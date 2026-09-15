# Native host TLS. Kept outside shared/: OpenSSL is not an L3 dependency.
#
# ★★ Windows 本机 OpenSSL 根自动探测 —— 只补「找不到」的洞,不改变 FindOpenSSL 本身的任何行为:
#    Shlib 安装器把 OpenSSL 装在 `C:/Program Files/OpenSSL-Win64`(带空格 + 版本后缀),
#    CMake 4.3 的 FindOpenSSL 默认前缀集(OpenSSL / OpenSSL-Win64 / (x86)/OpenSSL-Win64 …)
#    恰好不覆盖该布局,本机裸配置必然 NOT FOUND(civ.log 实录);
#    GitHub CI runner 自带的 OpenSSL 在默认搜索路径内,走不到本段;
#    ⇒ 仅当 OPENSSL_ROOT_DIR 在 cache 与环境中都未设置且 WIN32+MSVC 时,按序取第一个
#      存在 include/openssl/ssl.h 的候选根写入 CACHE(标记「探测所得」);
#      显式传入 OPENSSL_ROOT_DIR 时本段完全不生效。
if(WIN32 AND MSVC AND NOT DEFINED CACHE{OPENSSL_ROOT_DIR} AND NOT DEFINED ENV{OPENSSL_ROOT_DIR})
    foreach(_sa_openssl_probe_root
            "C:/Program Files/OpenSSL-Win64"
            "C:/Program Files (x86)/OpenSSL-Win64"
            "C:/OpenSSL-Win64"
            "C:/Program Files/OpenSSL-Win32"
            "C:/OpenSSL-Win32"
            "C:/OpenSSL")
        if(EXISTS "${_sa_openssl_probe_root}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_sa_openssl_probe_root}" CACHE PATH
                "OpenSSL 根目录(自动探测所得,非显式指定;显式传入 -DOPENSSL_ROOT_DIR 可覆盖)")
            break()
        endif()
    endforeach()
    unset(_sa_openssl_probe_root)
endif()
find_package(OpenSSL 1.1.1 REQUIRED)
add_library(sa_tls STATIC Channel.cpp)
target_include_directories(sa_tls PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(sa_tls PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_compile_features(sa_tls PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(sa_tls PUBLIC /utf-8)
endif()
if(COMMAND sa_apply_warnings)
    sa_apply_warnings(sa_tls)
elseif(COMMAND sa_client_apply_warnings)
    sa_client_apply_warnings(sa_tls)
endif()
