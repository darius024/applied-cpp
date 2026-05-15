cmake_minimum_required(VERSION 3.15)
project(grpc_proto CXX)
set(CMAKE_CXX_STANDARD 17)

# ── Dependencies ───────────────────────────────────────────────────────
find_package(Protobuf REQUIRED)
find_package(gRPC     CONFIG REQUIRED)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protos)
set(GEN_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

# ── Helper: compile one .proto → static library ────────────────────────
# Usage: compile_proto(<lib_name> <proto_file> [HAS_SERVICE])
# Creates a static lib <lib_name> exposing generated headers via GEN_DIR.
function(compile_proto LIB PROTO_FILE)
  get_filename_component(STEM ${PROTO_FILE} NAME_WE)

  set(PB_CC   ${GEN_DIR}/${STEM}.pb.cc)
  set(PB_H    ${GEN_DIR}/${STEM}.pb.h)
  set(GRPC_CC ${GEN_DIR}/${STEM}.grpc.pb.cc)
  set(GRPC_H  ${GEN_DIR}/${STEM}.grpc.pb.h)

  add_custom_command(
    OUTPUT  ${PB_CC} ${PB_H} ${GRPC_CC} ${GRPC_H}
    COMMAND protoc
            --proto_path=${PROTO_DIR}
            --cpp_out=${GEN_DIR}
            --grpc_out=${GEN_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating C++ from ${STEM}.proto"
  )

  add_library(${LIB} STATIC ${PB_CC} ${GRPC_CC})
  target_include_directories(${LIB} PUBLIC ${GEN_DIR} ${Protobuf_INCLUDE_DIRS})
  target_link_libraries(${LIB} PUBLIC gRPC::grpc++ protobuf::libprotobuf)
endfunction()

# ── Proto libraries ────────────────────────────────────────────────────
compile_proto(person_proto   ${PROTO_DIR}/person.proto)
compile_proto(advanced_proto ${PROTO_DIR}/advanced.proto)
compile_proto(metrics_proto  ${PROTO_DIR}/metrics.proto)

# ── Helper: build one example executable ──────────────────────────────
function(add_example TARGET SRC PROTO_LIB)
  add_executable(${TARGET} ${SRC})
  target_include_directories(${TARGET} PRIVATE ${GEN_DIR})
  target_link_libraries(${TARGET} PRIVATE
    ${PROTO_LIB}
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
  )
endfunction()

# ── Executables ────────────────────────────────────────────────────────
add_example(01_proto_basics        01_proto_basics.cpp        person_proto)
add_example(02_proto_advanced      02_proto_advanced.cpp      advanced_proto)
add_example(03_proto_arena         03_proto_arena.cpp         person_proto)
add_example(04_grpc_unary          04_grpc_unary.cpp          metrics_proto)
add_example(05_grpc_server_stream  05_grpc_server_stream.cpp  metrics_proto)
add_example(06_grpc_client_stream  06_grpc_client_stream.cpp  metrics_proto)
add_example(07_grpc_bidi_stream    07_grpc_bidi_stream.cpp    metrics_proto)
add_example(08_grpc_async          08_grpc_async.cpp          metrics_proto)
add_example(09_grpc_interceptors   09_grpc_interceptors.cpp   metrics_proto)
add_example(10_grpc_deadline_cancel 10_grpc_deadline_cancel.cpp metrics_proto)
