#include "Types.h"

#include <algorithm>
#include <cstring>

namespace serverengine::data::sql {

void Result::fail(se_status code, const char* diagnostic) noexcept
{
    status = code;
    affected_rows = 0;
    last_insert_id = 0;
    columns.clear();
    rows.clear();
    const auto length = (std::min)(std::strlen(diagnostic), message.size() - 1);
    std::memcpy(message.data(), diagnostic, length);
    message[length] = '\0';
}

} // namespace serverengine::data::sql
