#pragma once

#include <tuple>
#include <algorithm>
#include <type_traits>
#include <iostream>
#include <vector>
#include <array>
#include <memory>
#include <string>
#include <iterator>
#include <cassert>

extern "C"
{
#include "wasm3.h"
}



namespace wasm3 {
    /** @cond */
    namespace detail {
        typedef uint64_t *stack_type;
        typedef void *mem_type;
        template<typename T, typename...> struct first_type { typedef T type; };

        typedef const void *(*m3_api_raw_fn)(IM3Runtime, uint64_t *, void *);

        template<typename T>
        void arg_from_stack(T &dest, stack_type &_sp, [[maybe_unused]] mem_type mem) {
            m3ApiGetArg(T, tmp);
            dest = tmp;
        }

        template<typename T>
        void arg_from_stack(T* &dest, stack_type &_sp, [[maybe_unused]] mem_type _mem) {
            m3ApiGetArgMem(T*, tmp);
            dest = tmp;
        };

        template<typename T>
        void arg_from_stack(const T* &dest, stack_type &_sp, [[maybe_unused]] mem_type _mem) {
            m3ApiGetArgMem(const T*, tmp);
            dest = tmp;
        };

        template<char c>
        struct m3_sig {
            static const char value = c;
        };
        template<typename T, typename = void> struct m3_type_to_sig;

        template<typename T, typename U>
        using is_enum_of_t = typename std::enable_if<std::is_enum<T>::value
            && std::is_same<std::underlying_type_t<T>, U>::value>::type;

        template<typename T>
        struct m3_type_to_sig<T, is_enum_of_t<T, int32_t>> : m3_sig<'i'> {};

        template<class T>
        struct m3_type_to_sig<T, is_enum_of_t<T, int64_t>> : m3_sig<'I'> {};

        template<> struct m3_type_to_sig<int32_t> : m3_sig<'i'> {};
        template<> struct m3_type_to_sig<int64_t> : m3_sig<'I'> {};
        template<> struct m3_type_to_sig<float>   : m3_sig<'f'> {};
        template<> struct m3_type_to_sig<double>  : m3_sig<'F'> {};
        template<> struct m3_type_to_sig<void>    : m3_sig<'v'> {};
        template<> struct m3_type_to_sig<void *>  : m3_sig<'*'> {};
        template<> struct m3_type_to_sig<const void *> : m3_sig<'*'> {};


        template<typename Ret, typename ... Args>
        struct m3_signature {
            constexpr static size_t n_args = sizeof...(Args);
            constexpr static const char value[n_args + 4] = {
                    m3_type_to_sig<Ret>::value,
                    '(',
                    m3_type_to_sig<Args>::value...,
                    ')',
                    0
            };
        };

        template <typename ...Args>
        static void get_args_from_stack(stack_type &sp, mem_type mem, std::tuple<Args...> &tuple) {
            std::apply([&](auto &... item) {
                (arg_from_stack(item, sp, mem), ...);
            }, tuple);
        }

        template<typename Func>
        struct wrap_helper;

        template <typename Ret, typename ...Args>
        struct wrap_helper<Ret(Args...)> {
            using Func = Ret(Args...);
            static const void *wrap_fn([[maybe_unused]] IM3Runtime rt, IM3ImportContext _ctx, stack_type _sp, mem_type mem) {
                std::tuple<Args...> args;
                // The order here matters: m3ApiReturnType should go before calling get_args_from_stack,
                // since both modify `_sp`, and the return value on the stack is reserved before the arguments.
                m3ApiReturnType(Ret);
                get_args_from_stack(_sp, mem, args);
                Func* function = reinterpret_cast<Func*>(_ctx->userdata);
                Ret r = std::apply(function, args);
                m3ApiReturn(r);
            }
        };

        template <typename ...Args>
        struct wrap_helper<void(Args...)> {
            using Func = void(Args...);
            static const void *wrap_fn([[maybe_unused]] IM3Runtime rt, IM3ImportContext _ctx, stack_type sp, mem_type mem) {
                std::tuple<Args...> args;
                get_args_from_stack(sp, mem, args);
                Func* function = reinterpret_cast<Func*>(_ctx->userdata);
                std::apply(function, args);
                m3ApiSuccess();
            }
        };

        template<typename Func>
        class m3_wrapper;

        template<typename Ret, typename ... Args>
        class m3_wrapper<Ret(Args...)> {
        public:
            static M3Result link(IM3Module io_module,
                                 const char *const i_moduleName,
                                 const char *const i_functionName,
                                 Ret (*function)(Args...)) {

                return m3_LinkRawFunctionEx(io_module, i_moduleName, i_functionName,
                                            m3_signature<Ret, Args...>::value,
                                            &wrap_helper<Ret(Args...)>::wrap_fn,
                                            reinterpret_cast<void*>(function));
            }
        };
    } // namespace detail
    /** @endcond */

    class wasm_module;
    class wasm_runtime;
    class wasm_function;

    /**
     * Exception thrown for wasm3 errors.
     *
     * Use error:what() to get the reason as a string.
     */
    class error : public std::runtime_error {
    public:
        explicit error(M3Result err) : std::runtime_error(err) {}
    };

    /** @cond */
    namespace detail {
        static inline void check_error(M3Result err) {
            if (err != m3Err_none) {
                throw error(err);
            }
        }
    } // namespace detail
    /** @endcond */


    /**
     * Wrapper for WASM3 environment.
     *
     * Runtimes, modules are owned by an environment.
     */
    class wasm_environment {
    public:
        wasm_environment() {
            m_env.reset(m3_NewEnvironment(), m3_FreeEnvironment);
            if (m_env == nullptr) {
                throw std::bad_alloc();
            }
        }

        /**
         * Create new runtime
         *
         * @param stack_size_bytes  size of the WASM stack for this runtime
         * @return runtime object
         */
        wasm_runtime new_runtime(uint32_t stack_size_bytes);

        /**
         * Parse a WASM module from file
         *
         * The parsed module is not loaded into any runtime. Use runtime::load to
         * load the module after parsing it.
         *
         * @param in  file (WASM binary)
         * @return module object
         */
        wasm_module parse_module(std::istream &in);

        /**
         * Parse a WASM module from binary data
         * 
         * @param data  pointer to the start of the binary
         * @param size  size of the binary
         * @return module object
         */
        wasm_module parse_module(const uint8_t *data, size_t size);

    protected:
        std::shared_ptr<struct M3Environment> m_env;
    };

    /**
     * Wrapper for the runtime, where modules are loaded and executed.
     */
    class wasm_runtime {
    public:
        /**
         * Load the module into runtime
         * @param mod  module parsed by environment::parse_module
         */
        void load(wasm_module &mod);

        /**
         * Get a function handle by name
         * 
         * If the function is not found, throws an exception.
         * @param name  name of a function, c-string
         * @return function object
         */
        wasm_function find_function(const char *name);

    protected:
        friend class wasm_environment;

        wasm_runtime(const std::shared_ptr<M3Environment> &env, uint32_t stack_size_bytes)
                : m_env(env) {
            m_runtime.reset(m3_NewRuntime(env.get(), stack_size_bytes, nullptr), &m3_FreeRuntime);
            if (m_runtime == nullptr) {
                throw std::bad_alloc();
            }
        }

        /* runtime extends the lifetime of the environment */
        std::shared_ptr<M3Environment> m_env;
        std::shared_ptr<M3Runtime> m_runtime;
    };

    /**
     * Module object holds a webassembly module
     *
     * It can be constructed by parsing a WASM binary using environment::parse_module.
     * Functions can be linked to the loaded module.
     * Once constructed, modules can be loaded into the runtime.
     */
    class wasm_module {
    public:
        /**
         * Link an external function.
         *
         * Throws an exception if the module doesn't reference a function with the given name.
         *
         * @tparam Func Function type (signature)
         * @param module  Name of the module to link the function to, or "*" to link to any module
         * @param function_name  Name of the function (as referenced by the module)
         * @param function  Function to link (a function pointer)
         */
        template<typename Func>
        void link(const char *module, const char *function_name, Func *function);

        /**
         * Same as module::link, but doesn't throw an exception if the function is not referenced.
         */
        template<typename Func>
        void link_optional(const char *module, const char *function_name, Func *function);

    public:

        auto get_module() -> std::shared_ptr<M3Module>
        {
            return m_module;
        }

    protected:
        friend class wasm_environment;
        friend class wasm_runtime;

        wasm_module(const std::shared_ptr<M3Environment> &env, std::istream &in_wasm) {
            in_wasm.unsetf(std::ios::skipws);
            std::copy(std::istream_iterator<uint8_t>(in_wasm),
                      std::istream_iterator<uint8_t>(),
                      std::back_inserter(m_moduleRawData));
            parse(env.get(), m_moduleRawData.data(), m_moduleRawData.size());
        }

        wasm_module(const std::shared_ptr<M3Environment> &env, const uint8_t *data, size_t size) : m_env(env) {
            m_moduleRawData = std::vector<uint8_t>{data, data + size};
            parse(env.get(), m_moduleRawData.data(), m_moduleRawData.size());
        }

        void parse(IM3Environment env, const uint8_t *data, size_t size) {
            IM3Module p;
            M3Result err = m3_ParseModule(env, &p, data, static_cast<uint32_t>(size));
            detail::check_error(err);
            m_module.reset(p, [this](IM3Module module) {
                if (!m_loaded) {
                    m3_FreeModule(module);
                }
            });
        }

        void load_into(IM3Runtime runtime) {
            M3Result err = m3_LoadModule(runtime, m_module.get());
            detail::check_error(err);
            m_loaded = true;
        }


        std::shared_ptr<M3Environment> m_env;
        std::shared_ptr<M3Module> m_module;
        bool m_loaded = false;
        std::vector<uint8_t> m_moduleRawData {};
    };


    /**
     * Handle of a function. Can be obtained from runtime::find_function method by name.
     */
    class wasm_function {
    public:
        /**
         * Call the function with the provided arguments, expressed as strings.
         *
         * Arguments are passed as strings. WASM3 automatically converts them into the types expected
         * by the function being called.
         *
         * Note that the type of the return value must be explicitly specified as a template argument.
         *
         * @return the return value of the function.
         */
        template<typename Ret, typename ... Args>
        typename detail::first_type<Ret,
                typename std::enable_if<std::is_convertible<Args, const char*>::value>::type...>::type
        call_argv(Args... args) {
            /* std::enable_if above checks that all argument types are convertible const char* */
            const char* argv[] = {args...};
            M3Result res = m3_CallArgv(m_func, sizeof...(args), argv);
            detail::check_error(res);
            Ret ret;
            res = m3_GetResults(m_func, 1, &ret);
            detail::check_error(res);
            return ret;
        }

        template<typename ... Args>
        typename detail::first_type<void, 
                typename std::enable_if<std::is_convertible<Args, const char*>::value>::type...>::type
        call_argv(Args... args) {
            /* std::enable_if above checks that all argument types are convertible const char* */
            const char* argv[] = {args...};
            M3Result res = m3_CallArgv(m_func, sizeof...(args), argv);
            detail::check_error(res);
        }

        /**
         * Call the function with the provided arguments (int/float types).
         *
         * Note that the type of the return value must be explicitly specified as a template argument.
         *
         * @return the return value of the function or void.
         */
        template<typename Ret = void, typename ... Args>
        Ret call(Args... args) {
            std::array<const void*, sizeof...(args)> arg_ptrs{ reinterpret_cast<const void*>(&args)... };
            M3Result res = m3_Call(m_func, arg_ptrs.size(), arg_ptrs.data());
            detail::check_error(res);

            if constexpr (!std::is_void<Ret>::value) {
                Ret ret;
                const void* ret_ptrs[] = { &ret };
                res = m3_GetResults(m_func, 1, ret_ptrs);
                detail::check_error(res);
                return ret; 
            }
        }

        template<typename ...Ret, typename ... Args>
        std::tuple<Ret...> call_tuple(Args... args) {
            call(args...);

            std::tuple<Ret...> ret;
            const auto func = m_func;

            auto res = std::apply([func](auto&&... retRefs) {
                return m3_GetResultsV(func, &retRefs...);
            }, ret);

            detail::check_error(res);
            return ret;
        }

        template<typename Ret, typename ... Args>
        Ret call_mapped(Args... args) {
            call(args...);

            Ret ret;
            const auto res = m3_GetResultsBuffer(m_func, sizeof(ret), &ret);
            detail::check_error(res);

            return ret; 
        }


    protected:
        friend class wasm_runtime;

        wasm_function(const std::shared_ptr<M3Runtime> &runtime, const char *name) : m_runtime(runtime) {
            M3Result err = m3_FindFunction(&m_func, runtime.get(), name);
            detail::check_error(err);
            assert(m_func != nullptr);
        }

        std::shared_ptr<M3Runtime> m_runtime;
        M3Function *m_func = nullptr;
    };

    inline wasm_runtime wasm_environment::new_runtime(uint32_t stack_size_bytes) {
        return wasm_runtime(m_env, stack_size_bytes);
    }

    inline wasm_module wasm_environment::parse_module(std::istream &in) {
        return wasm_module(m_env, in);
    }

    inline wasm_module wasm_environment::parse_module(const uint8_t *data, size_t size) {
        return wasm_module(m_env, data, size);
    }

    inline void wasm_runtime::load(wasm_module &mod) {
        mod.load_into(m_runtime.get());
    }

    inline wasm_function wasm_runtime::find_function(const char *name) {
        return wasm_function(m_runtime, name);
    }

    template<typename Func>
    void wasm_module::link(const char *module, const char *function_name, Func *function) {
        M3Result ret = detail::m3_wrapper<Func>::link(m_module.get(), module, function_name, function);
        detail::check_error(ret);
    }

    template<typename Func>
    void wasm_module::link_optional(const char *module, const char *function_name, Func *function) {
        M3Result ret = detail::m3_wrapper<Func>::link(m_module.get(), module, function_name, function);
        if (ret == m3Err_functionLookupFailed) {
            return;
        }
        detail::check_error(ret);
    }

} // namespace wasm3
