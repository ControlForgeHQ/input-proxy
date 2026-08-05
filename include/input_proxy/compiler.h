#ifndef INPUT_PROXY_COMPILER_H
#define INPUT_PROXY_COMPILER_H

#if defined(__GNUC__) || defined(__clang__)
#define INPUT_PROXY_ATTRIBUTE_NODISCARD __attribute__((warn_unused_result))
#define INPUT_PROXY_ATTRIBUTE_UNUSED __attribute__((unused))
#else
#define INPUT_PROXY_ATTRIBUTE_NODISCARD
#define INPUT_PROXY_ATTRIBUTE_UNUSED
#endif

#endif