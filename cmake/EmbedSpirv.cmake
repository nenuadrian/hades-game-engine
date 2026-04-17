# EmbedSpirv.cmake — run as `cmake -P` with -DINPUT=... -DOUTPUT=... -DSYMBOL=...
# Reads a SPIR-V binary file and writes a C++ header containing it as a
# uint32_t array (SPIR-V is naturally 4-byte aligned).

if(NOT INPUT OR NOT OUTPUT OR NOT SYMBOL)
  message(FATAL_ERROR "EmbedSpirv.cmake requires -DINPUT=, -DOUTPUT=, -DSYMBOL=")
endif()

file(READ "${INPUT}" hex_content HEX)
string(LENGTH "${hex_content}" hex_len)
math(EXPR byte_count "${hex_len} / 2")
math(EXPR word_count "${byte_count} / 4")

set(words "")
set(column 0)
set(i 0)
while(i LESS hex_len)
  # SPIR-V is little-endian; read 8 hex chars and reverse byte order.
  string(SUBSTRING "${hex_content}" ${i} 2 b0)
  math(EXPR j "${i} + 2")
  string(SUBSTRING "${hex_content}" ${j} 2 b1)
  math(EXPR j "${i} + 4")
  string(SUBSTRING "${hex_content}" ${j} 2 b2)
  math(EXPR j "${i} + 6")
  string(SUBSTRING "${hex_content}" ${j} 2 b3)
  set(word "0x${b3}${b2}${b1}${b0}")
  if(column EQUAL 0)
    set(words "${words}  ${word}")
  else()
    set(words "${words}, ${word}")
  endif()
  math(EXPR column "${column} + 1")
  if(column EQUAL 8)
    set(words "${words},\n")
    set(column 0)
  endif()
  math(EXPR i "${i} + 8")
endwhile()

set(header_content "// Auto-generated from ${INPUT}. Do not edit.\n")
set(header_content "${header_content}#pragma once\n")
set(header_content "${header_content}#include <cstdint>\n")
set(header_content "${header_content}#include <cstddef>\n\n")
set(header_content "${header_content}static const uint32_t ${SYMBOL}[] = {\n${words}\n};\n")
set(header_content "${header_content}static const size_t ${SYMBOL}_size = sizeof(${SYMBOL});\n")

file(WRITE "${OUTPUT}" "${header_content}")
