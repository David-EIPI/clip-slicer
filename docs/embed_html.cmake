# SPDX-FileCopyrightText: 2026 David Shirvanyants
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "INPUT and OUTPUT are required")
endif()

file(READ "${INPUT}" html_hex HEX)
string(LENGTH "${html_hex}" hex_length)
file(WRITE "${OUTPUT}"
    "// Generated from docs/build/main.html by docs/embed_html.cmake.\n")
set(offset 0)
while(offset LESS hex_length)
    math(EXPR remaining "${hex_length} - ${offset}")
    if(remaining GREATER 32)
        set(line_length 32)
    else()
        set(line_length "${remaining}")
    endif()
    string(SUBSTRING "${html_hex}" ${offset} ${line_length} line)
    string(REGEX REPLACE "(..)" "0x\\1, " line "${line}")
    string(REGEX REPLACE ", $" "," line "${line}")
    file(APPEND "${OUTPUT}" "    ${line}\n")
    math(EXPR offset "${offset} + ${line_length}")
endwhile()
