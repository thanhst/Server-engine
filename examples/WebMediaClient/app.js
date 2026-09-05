'use strict';
const byId = id => document.getElementById(id);
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
let activeCall = null;
const logLines = [];
function log(message) {
  logLines.push(`${new Date().toLocaleTimeString()} ${message}`);
  if (logLines.length > 60) logLines.shift();
  byId('log').textContent = logLines.join('\n');
}

byId('profile').onclick = async () => {
  try {
    const response = await fetch('/api/profile', { cache: 'no-store' });
    byId('profile-result').textContent = `HTTP ${response.status}\n${await response.text()}`;
  } catch (error) { byId('profile-result').textContent = error.message; }
};
byId('binary').onclick = async () => {
  try {
    const file = byId('file').files[0];
    if (!file) throw new Error('Hãy chọn một file trước.');
    const input = new Uint8Array(await file.slice(0, 16384).arrayBuffer());
    const response = await fetch('/api/echo', {
      method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: input
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const output = new Uint8Array(await response.arrayBuffer());
    if (output.length !== input.length || !input.every((value, i) => output[i] === value))
      throw new Error('Dữ liệu trả về khác dữ liệu đã gửi.');
    byId('binary-result').textContent = `${input.length} byte khớp hoàn toàn. Đây là truyền một khối byte, chưa phải phát video.`;
  } catch (error) { byId('binary-result').textContent = error.message; }
};

class Call {
  constructor() {
    this.stopped = false;
    this.pendingIce = [];
    this.messages = Promise.resolve();
    this.messageCount = 0;
  }
  async start() {
    if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia)
      throw new Error('Camera/mic cần trang HTTPS đáng tin cậy.');
    this.media = await navigator.mediaDevices.getUserMedia({ audio: true, video: byId('camera').checked });
    if (this.stopped) { this.media.getTracks().forEach(track => track.stop()); return; }
    byId('local').srcObject = this.media;
    // Empty ICE servers makes the demo self-contained: no third-party service
    // receives signaling or connection metadata. Configure your own TURN for WAN.
    this.peer = new RTCPeerConnection({ iceServers: [] });
    for (const track of this.media.getTracks()) this.peer.addTrack(track, this.media);
    this.peer.ontrack = event => {
      if (!this.stopped && event.streams[0]) byId('remote').srcObject = event.streams[0];
    };
    this.peer.onicecandidate = event => {
      if (event.candidate && !this.stopped) this.signal({ candidate: event.candidate.toJSON() });
    };
    this.peer.onconnectionstatechange = () => {
      if (!this.stopped) log(`WebRTC: ${this.peer.connectionState}`);
    };
    const url = new URL(location.href);
    url.protocol = 'wss:'; url.port = '9554'; url.pathname = '/signal'; url.search = ''; url.hash = '';
    this.socket = new WebSocket(url);
    this.socket.binaryType = 'arraybuffer';
    this.socket.onopen = () => {
      if (this.stopped) return;
      log('WSS đã mở. Chờ người thứ hai.');
      this.heartbeat = setInterval(() => this.send('PING'), 10000);
    };
    this.socket.onmessage = event => {
      if (this.stopped) return;
      if (!(event.data instanceof ArrayBuffer) || event.data.byteLength > 65536 || ++this.messageCount > 128) {
        this.stop('Thông điệp signaling vượt giới hạn.'); return;
      }
      // Serialize SDP/ICE operations; async WebSocket handlers otherwise overlap.
      this.messages = this.messages.then(() => this.receive(decoder.decode(event.data)))
        .catch(error => this.stop(error.message))
        .finally(() => { --this.messageCount; });
    };
    this.socket.onerror = () => this.stop('WSS lỗi: kiểm tra chứng chỉ và server.');
    this.socket.onclose = () => this.stop('Phòng đã đóng. Có thể tham gia lại.');
  }
  send(message) {
    if (this.stopped) return;
    if (this.socket?.readyState !== WebSocket.OPEN || this.socket.bufferedAmount > 262144) {
      this.stop('Signaling chưa sẵn sàng hoặc hàng đợi gửi đã đầy.'); return;
    }
    const bytes = encoder.encode(message);
    if (bytes.length > 65536) { this.stop('SDP/ICE vượt giới hạn message.'); return; }
    this.socket.send(bytes); // DLL WebSocket endpoint uses binary messages.
  }
  signal(value) { this.send(`SIGNAL\n${JSON.stringify(value)}`); }
  async receive(message) {
    if (this.stopped) return;
    if (message === 'PONG' || message === 'WAIT') return;
    if (message === 'PEER_LEFT' || message === 'FULL') {
      this.stop(message === 'FULL' ? 'Phòng đã đủ hai người.' : 'Người bên kia đã rời.'); return;
    }
    const peer = this.peer;
    if (message === 'OFFER') {
      await peer.setLocalDescription(await peer.createOffer());
      if (!this.stopped) this.signal({ description: peer.localDescription.toJSON() });
      return;
    }
    if (!message.startsWith('SIGNAL\n')) throw new Error('Thông điệp signaling không hợp lệ.');
    const signal = JSON.parse(message.slice(7));
    if (signal.description) {
      if (!['offer', 'answer'].includes(signal.description.type) || typeof signal.description.sdp !== 'string')
        throw new Error('SDP không hợp lệ.');
      await peer.setRemoteDescription(signal.description);
      if (this.stopped) return;
      for (const candidate of this.pendingIce.splice(0)) {
        await peer.addIceCandidate(candidate);
        if (this.stopped) return;
      }
      if (signal.description.type === 'offer') {
        await peer.setLocalDescription(await peer.createAnswer());
        if (!this.stopped) this.signal({ description: peer.localDescription.toJSON() });
      }
    } else if (signal.candidate) {
      if (peer.remoteDescription) await peer.addIceCandidate(signal.candidate);
      else {
        if (this.pendingIce.length >= 128) throw new Error('Quá nhiều ICE candidate chưa xử lý.');
        this.pendingIce.push(signal.candidate);
      }
    } else throw new Error('Thiếu SDP hoặc ICE candidate.');
  }
  stop(message) {
    if (this.stopped) return;
    this.stopped = true;
    clearInterval(this.heartbeat);
    this.socket?.close();
    this.peer?.close();
    this.media?.getTracks().forEach(track => track.stop());
    this.pendingIce.length = 0;
    if (activeCall === this) {
      activeCall = null;
      byId('local').srcObject = null; byId('remote').srcObject = null;
      byId('join').disabled = false; byId('leave').disabled = true;
    }
    if (message) log(message);
  }
}
byId('join').onclick = async () => {
  if (activeCall) return;
  const call = new Call(); activeCall = call;
  byId('join').disabled = true; byId('leave').disabled = false;
  try { await call.start(); } catch (error) { call.stop(error.message); }
};
byId('leave').onclick = () => activeCall?.stop('Đã dừng camera và microphone.');
window.addEventListener('pagehide', () => activeCall?.stop());
