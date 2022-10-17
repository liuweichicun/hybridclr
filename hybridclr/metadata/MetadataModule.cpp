#include "MetadataModule.h"

#include "Baselib.h"
#include "os/Atomic.h"
#include "os/Mutex.h"
#include "os/File.h"
#include "vm/Exception.h"
#include "vm/String.h"
#include "vm/Assembly.h"
#include "vm/Class.h"
#include "vm/Object.h"
#include "vm/Image.h"
#include "vm/MetadataLock.h"
#include "utils/Logging.h"
#include "utils/MemoryMappedFile.h"
#include "utils/Memory.h"

#include "../interpreter/InterpreterModule.h"

#include "InterpreterImage.h"
#include "AOTHomologousImage.h"
#include "ReversePInvokeMethodStub.h"

using namespace il2cpp;

namespace hybridclr
{

namespace metadata
{

    std::unordered_map<const MethodInfo*, const ReversePInvokeInfo*> MetadataModule::s_methodInfo2ReverseInfos;
    std::unordered_map<Il2CppMethodPointer, const ReversePInvokeInfo*> MetadataModule::s_methodPointer2ReverseInfos;
    std::unordered_map<const char*, int32_t, CStringHash, CStringEqualTo> MetadataModule::s_methodSig2Indexs;
    std::vector<ReversePInvokeInfo> MetadataModule::s_reverseInfos;

    static baselib::ReentrantLock g_reversePInvokeMethodLock;

    void MetadataModule::InitReversePInvokeInfo()
    {
        for (int32_t i = 0; ; i++)
        {
            ReversePInvokeMethodData& data = g_reversePInvokeMethodStub[i];
            if (data.methodPointer == nullptr)
            {
                break;
            }
            s_reverseInfos.push_back({ i, data.methodPointer, nullptr });
            auto it = s_methodSig2Indexs.find(data.methodSig);
            if (it == s_methodSig2Indexs.end())
            {
                s_methodSig2Indexs.insert({ data.methodSig, i });
            }
        }
        s_methodInfo2ReverseInfos.reserve(s_reverseInfos.size() * 2);
        s_methodPointer2ReverseInfos.reserve(s_reverseInfos.size() * 2);
        for (ReversePInvokeInfo& rpi : s_reverseInfos)
        {
            s_methodPointer2ReverseInfos.insert({ rpi.methodPointer, &rpi });
        }
    }

    void MetadataModule::Initialize()
    {
        InitReversePInvokeInfo();
        InterpreterImage::Initialize();
    }

    Il2CppMethodPointer MetadataModule::GetReversePInvokeWrapper(const Il2CppImage* image, const MethodInfo* method)
    {
        if (!hybridclr::metadata::IsStaticMethod(method))
        {
            return nullptr;
        }
        il2cpp::os::FastAutoLock lock(&g_reversePInvokeMethodLock);
        auto it = s_methodInfo2ReverseInfos.find(method);
        if (it != s_methodInfo2ReverseInfos.end())
        {
            return it->second->methodPointer;
        }


        char sigName[1000];
        interpreter::ComputeSignature(method, false, sigName, sizeof(sigName) - 1);
        auto it2 = s_methodSig2Indexs.find(sigName);
        if (it2 == s_methodSig2Indexs.end())
        {
            TEMP_FORMAT(methodSigBuf, "GetReversePInvokeWrapper fail. not find wrapper of method:%s", GetMethodNameWithSignature(method).c_str());
            RaiseExecutionEngineException(methodSigBuf);
        }

        ReversePInvokeMethodData& data = g_reversePInvokeMethodStub[it2->second];
        if (data.methodPointer == nullptr || std::strcmp(data.methodSig, sigName))
        {
            TEMP_FORMAT(methodSigBuf, "GetReversePInvokeWrapper fail. exceed max wrapper num of method:%s", GetMethodNameWithSignature(method).c_str());
            RaiseExecutionEngineException(methodSigBuf);
        }

        s_methodSig2Indexs[sigName] = it2->second + 1;

        ReversePInvokeInfo& rpi = s_reverseInfos[it2->second];
        rpi.methodInfo = method;
        s_methodInfo2ReverseInfos.insert({ method, &rpi });
        return rpi.methodPointer;
    }
}
}
