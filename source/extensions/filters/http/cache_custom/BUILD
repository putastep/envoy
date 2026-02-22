load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cache_filter_lib",
    srcs = [
        "cache_filter.cc",
        "cache_config.cc",
        "cache_manager.cc",
        "cache_entry_handle.cc",
    ],
    hdrs = [
        "cache_filter.h",
        "cache_config.h",
        "cache_manager.h",
        "common.h",
        "cache_entry_handle.h",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/cache_custom/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cache_filter_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/cache_custom/v3:pkg_cc_proto",
    ],
)