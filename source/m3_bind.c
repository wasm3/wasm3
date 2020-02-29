//
//  m3_bind.c
//
//  Created by Steven Massey on 4/29/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_exec.h"
#include "m3_env.h"
#include "m3_exception.h"


u8  ConvertTypeCharToTypeId (char i_code)
{
    switch (i_code) {
    case 'v': return c_m3Type_void;
    case 'i': return c_m3Type_i32;
    case 'I': return c_m3Type_i64;
    case 'f': return c_m3Type_f32;
    case 'F': return c_m3Type_f64;
    case '*': return c_m3Type_ptr;
    }
    return c_m3Type_none;
}


M3Result  SignatureToFuncType  (M3FuncType * o_functionType, ccstr_t i_signature)
{
    M3Result result = m3Err_none;                                       d_m3Assert (o_functionType->argTypes == NULL); // unfreed functype
    
    if (not o_functionType)
        _throw ("null function type");
    
    if (not i_signature)
        _throw ("null function signature");
    
    cstr_t sig = i_signature;
    
    bool hasReturn = false;
    o_functionType->numArgs = 0;
    
_   (m3Alloc (& o_functionType->argTypes, u8, strlen (i_signature)));
    
    bool parsingArgs = false;
    while (* sig)
    {
        char typeChar = * sig++;
        
        if (typeChar == '(')
        {
            if (not hasReturn)
                break;
            
            parsingArgs = true;
            continue;
        }
        else if ( typeChar == ' ')
            continue;
        else if (typeChar == ')')
            break;
        
        u8 type = ConvertTypeCharToTypeId (typeChar);
        
        if (not type)
            _throw ("unknown argument type char");
        
        if (not parsingArgs)
        {
            if (hasReturn)
                _throw ("malformed function signature; too many return types");
            
            hasReturn = true;
            
            // M3FuncType doesn't speak 'void'
            if (type == c_m3Type_void)
                type = c_m3Type_none;
            
            o_functionType->returnType = type;
        }
        else
        {
            if (type != c_m3Type_runtime)
            {
                if (type == c_m3Type_ptr)
                    type = c_m3Type_i32;
                
                o_functionType->argTypes [o_functionType->numArgs++] = type;
            }
        }
    }
    
    if (not hasReturn)
        _throw (m3Err_funcSignatureMissingReturnType);
    
    _catch: return result;
}


static
M3Result  ValidateSignature  (IM3Function i_function, ccstr_t i_linkingSignature)
{
    M3Result result = m3Err_none;

    M3FuncType ftype = {};
_   (SignatureToFuncType (& ftype, i_linkingSignature));

    if (not AreFuncTypesEqual (& ftype, i_function->funcType))
        _throw ("function signature mismatch");

    _catch:
    
    FuncType_Free (& ftype);
    
    return result;
}


typedef M3Result  (* M3Linker)  (IM3Module io_module,  IM3Function io_function,  const char * const i_signature,  const void * i_function);

M3Result  FindAndLinkFunction      (IM3Module       io_module,
                                    ccstr_t         i_moduleName,
                                    ccstr_t         i_functionName,
                                    ccstr_t         i_signature,
                                    voidptr_t       i_function,
                                    const M3Linker  i_linker)
{
    M3Result result = m3Err_functionLookupFailed;

    bool wildcardModule = (strcmp (i_moduleName, "*") == 0);
    
    for (u32 i = 0; i < io_module->numFunctions; ++i)
    {
        IM3Function f = & io_module->functions [i];

        if (f->import.moduleUtf8 and f->import.fieldUtf8)
        {
            if (strcmp (f->import.fieldUtf8, i_functionName) == 0 and
               (wildcardModule or strcmp (f->import.moduleUtf8, i_moduleName) == 0))
            {
                result = i_linker (io_module, f, i_signature, i_function);
                if (result) return result;
            }
        }
    }

    return result;
}

// --------------------------------------------------------------------------------------------------------------------------------------------------------------------------

M3Result  LinkRawFunction  (IM3Module io_module,  IM3Function io_function, ccstr_t signature,  const void * i_function)
{
    M3Result result = m3Err_none;                                                 d_m3Assert (io_module->runtime);

_try {
_   (ValidateSignature (io_function, signature));

    IM3CodePage page = AcquireCodePageWithCapacity (io_module->runtime, 2);

    if (page)
    {
        io_function->compiled = GetPagePC (page);
        io_function->module = io_module;

        EmitWord (page, op_CallRawFunction);
        EmitWord (page, i_function);

        ReleaseCodePage (io_module->runtime, page);
    }
    else _throw(m3Err_mallocFailedCodePage);

} _catch:
    return result;
}


M3Result  m3_LinkRawFunction  (IM3Module            io_module,
                              const char * const    i_moduleName,
                              const char * const    i_functionName,
                              const char * const    i_signature,
                              M3RawCall             i_function)
{
    return FindAndLinkFunction (io_module, i_moduleName, i_functionName, i_signature, (voidptr_t)i_function, LinkRawFunction);
}

// --------------------------------------------------------------------------------------------------------------------------------------------------------------------------

IM3Function  FindFunction    (IM3Module       io_module,
                              ccstr_t         i_moduleName,
                              ccstr_t         i_functionName,
                              ccstr_t         i_signature)
{
    bool wildcardModule = (strcmp (i_moduleName, "*") == 0);
    
    for (u32 i = 0; i < io_module->numFunctions; ++i)
    {
        IM3Function f = & io_module->functions [i];

        if (f->import.moduleUtf8 and f->import.fieldUtf8)
        {
            if (strcmp (f->import.fieldUtf8, i_functionName) == 0 and
               (wildcardModule or strcmp (f->import.moduleUtf8, i_moduleName) == 0))
            {
                return f;
            }
        }
    }

    return NULL;
}

M3Result  LinkRawFunctionEx  (IM3Module io_module,  IM3Function io_function, ccstr_t signature,  const void * i_function, void * cookie)
{
    M3Result result = m3Err_none;                                                 d_m3Assert (io_module->runtime);

_try {
_   (ValidateSignature (io_function, signature));

    IM3CodePage page = AcquireCodePageWithCapacity (io_module->runtime, 3);

    if (page)
    {
        io_function->compiled = GetPagePC (page);
        io_function->module = io_module;

        EmitWord (page, op_CallRawFunctionEx);
        EmitWord (page, i_function);
        EmitWord (page, cookie);

        ReleaseCodePage (io_module->runtime, page);
    }
    else _throw(m3Err_mallocFailedCodePage);

} _catch:
    return result;
}

M3Result  m3_LinkRawFunctionEx  (IM3Module            io_module,
                                const char * const    i_moduleName,
                                const char * const    i_functionName,
                                const char * const    i_signature,
                                M3RawCallEx           i_function,
                                void *                i_cookie)
{
    IM3Function f = FindFunction(io_module, i_moduleName, i_functionName, i_signature);
    if (f == NULL)
        return m3Err_functionLookupFailed;
    
    M3Result result = LinkRawFunctionEx(io_module, f, i_signature, (voidptr_t)i_function, i_cookie);
    return result;
}
