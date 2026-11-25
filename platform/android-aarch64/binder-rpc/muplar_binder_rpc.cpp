#include <binder/Binder.h>
#include <binder/BpBinder.h>
#include <binder/Parcel.h>
#include <binder/RpcServer.h>
#include <binder/RpcSession.h>
#include <jni.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <mutex>
#include <vector>
#include <map>
#include <cstring>
#include <new>
#include <cstdio>

using namespace android;


namespace
{
JavaVM *vm;
jclass parcelClass;
jmethodID parcelObtain;
jmethodID parcelRecycle;
jmethodID binderTransact;
jmethodID binderDescriptor;
jmethodID parcelWriteException;
jclass exceptionClass;
jclass binderClass;
jfieldID parcelPointer;
jclass remoteClass;
jfieldID remoteHandle;
jmethodID remoteConstructor;
jmethodID deathCallback;
std::mutex wrappersMutex;
class JavaRpcBinder;
std::vector<wp<JavaRpcBinder>> wrappers;
sp<RpcServer> localServer;
sp<RpcSession> localSession;
sp<IBinder> localRoot;
std::mutex serversMutex;
std::map<jlong, sp<RpcServer>> servers;
jlong nextServer = 1;
struct RemoteProxy {
    jweak object;
    jlong handle;
};
std::mutex remoteProxiesMutex;
std::map<IBinder *, RemoteProxy> remoteProxies;
struct LocalObject {
    sp<IBinder> binder;
    std::vector<uint8_t> encoding;
};
std::mutex parcelsMutex;
std::map<Parcel *, std::map<size_t, LocalObject>> localParcels;

class LocalRelay final : public BBinder
{
    status_t onTransact(uint32_t code,
                        const Parcel &data,
                        Parcel *reply,
                        uint32_t) override
    {
        if (code != FIRST_CALL_TRANSACTION || !reply)
            return UNKNOWN_TRANSACTION;
        sp<IBinder> value;
        status_t result = data.readNullableStrongBinder(&value);
        return result == OK ? reply->writeStrongBinder(value) : result;
    }
};

JNIEnv *environment(bool *attached)
{
    JNIEnv *env = nullptr;
    *attached =
        vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK;
    if (*attached && vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
        return nullptr;
    return env;
}

void fail(JNIEnv *env, const char *type, const char *message)
{
    jclass cls = env->FindClass(type);
    if (cls)
        env->ThrowNew(cls, message);
}

class JavaRpcBinder final : public BBinder
{
public:
    jobject object;
    String16 name;
    JavaRpcBinder(JNIEnv *env, jobject value) : object(env->NewGlobalRef(value))
    {
        auto descriptor = static_cast<jstring>(
            env->CallObjectMethod(value, binderDescriptor));
        if (descriptor) {
            const jchar *chars = env->GetStringChars(descriptor, nullptr);
            if (chars) {
                name = String16(reinterpret_cast<const char16_t *>(chars),
                                env->GetStringLength(descriptor));
                env->ReleaseStringChars(descriptor, chars);
            }
            env->DeleteLocalRef(descriptor);
        }
    }
    const String16 &getInterfaceDescriptor() const override { return name; }
    ~JavaRpcBinder() override
    {
        bool attached;
        JNIEnv *env = environment(&attached);
        if (env)
            env->DeleteGlobalRef(object);
        if (attached && env)
            vm->DetachCurrentThread();
    }

protected:
    status_t onTransact(uint32_t code,
                        const Parcel &data,
                        Parcel *reply,
                        uint32_t flags) override
    {
        bool attached;
        JNIEnv *env = environment(&attached);
        if (!env)
            return DEAD_OBJECT;
        jobject input = env->CallStaticObjectMethod(
            parcelClass, parcelObtain, reinterpret_cast<jlong>(&data));
        jobject output =
            reply ? env->CallStaticObjectMethod(parcelClass, parcelObtain,
                                                reinterpret_cast<jlong>(reply))
                  : nullptr;
        jboolean result = JNI_FALSE;
        if (!env->ExceptionCheck()) {
            result = env->CallBooleanMethod(object, binderTransact, code, input,
                                            output, flags);
        }
        bool error = env->ExceptionCheck();
        if (error) {
            jthrowable exception = env->ExceptionOccurred();
            env->ExceptionClear();
            if (reply && env->IsInstanceOf(exception, exceptionClass)) {
                reply->setDataSize(0);
                reply->setDataPosition(0);
                env->CallVoidMethod(output, parcelWriteException, exception);
                error = env->ExceptionCheck();
                result = JNI_TRUE;
            }
            env->DeleteLocalRef(exception);
            if (error) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
        if (input) {
            env->CallVoidMethod(input, parcelRecycle);
            env->DeleteLocalRef(input);
        }
        if (output) {
            env->CallVoidMethod(output, parcelRecycle);
            env->DeleteLocalRef(output);
        }
        if (attached)
            vm->DetachCurrentThread();
        return error ? UNKNOWN_ERROR : result ? OK : UNKNOWN_TRANSACTION;
    }
};

sp<IBinder> fromJava(JNIEnv *env, jobject value)
{
    if (!value)
        return nullptr;
    if (env->IsInstanceOf(value, remoteClass)) {
        auto handle = reinterpret_cast<sp<IBinder> *>(
            env->GetLongField(value, remoteHandle));
        if (handle)
            return *handle;
        fail(env, "android/os/DeadObjectException", "Released RPC binder");
        return nullptr;
    }
    if (!env->IsInstanceOf(value, binderClass)) {
        fail(env, "java/lang/UnsupportedOperationException",
             "Binder is neither a local Binder nor an RPC proxy");
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(wrappersMutex);
    for (auto i = wrappers.begin(); i != wrappers.end();) {
        auto binder = i->promote();
        if (!binder) {
            i = wrappers.erase(i);
            continue;
        }
        if (env->IsSameObject(value, binder->object))
            return binder;
        ++i;
    }
    auto binder = sp<JavaRpcBinder>::make(env, value);
    wrappers.emplace_back(binder);
    return binder;
}

jobject toJava(JNIEnv *env, const sp<IBinder> &value)
{
    if (!value)
        return nullptr;
    {
        std::lock_guard<std::mutex> lock(wrappersMutex);
        for (auto i = wrappers.begin(); i != wrappers.end();) {
            auto binder = i->promote();
            if (!binder) {
                i = wrappers.erase(i);
                continue;
            }
            if (binder.get() == value.get())
                return env->NewLocalRef(binder->object);
            ++i;
        }
    }
    std::lock_guard<std::mutex> lock(remoteProxiesMutex);
    auto entry = remoteProxies.find(value.get());
    if (entry != remoteProxies.end()) {
        jobject existing = env->NewLocalRef(entry->second.object);
        if (existing)
            return existing;
        env->DeleteWeakGlobalRef(entry->second.object);
        remoteProxies.erase(entry);
    }
    auto handle = new sp<IBinder>(value);
    jlong pointer = reinterpret_cast<jlong>(handle);
    jobject result = env->NewObject(remoteClass, remoteConstructor, pointer);
    if (!result) {
        delete handle;
        return nullptr;
    }
    jweak weak = env->NewWeakGlobalRef(result);
    if (weak)
        remoteProxies.emplace(value.get(), RemoteProxy{weak, pointer});
    return result;
}

jlong createParcel(JNIEnv *env, jclass)
{
    if (!localSession) {
        fail(env, "java/lang/IllegalStateException",
             "Binder RPC session not initialized");
        return 0;
    }
    auto parcel = new Parcel();
    parcel->markForRpc(localSession);
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        localParcels.emplace(parcel, std::map<size_t, LocalObject>{});
    }
    return reinterpret_cast<jlong>(parcel);
}

void recycleParcel(JNIEnv *, jclass, jlong pointer)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        localParcels[parcel].clear();
    }
    parcel->~Parcel();
    new (parcel) Parcel();
    parcel->markForRpc(localSession);
}

void destroyParcel(JNIEnv *, jclass, jlong pointer)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        localParcels.erase(parcel);
    }
    delete parcel;
}

void markForBinder(JNIEnv *env, jclass, jlong pointer, jobject value)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    auto binder = fromJava(env, value);
    if (env->ExceptionCheck())
        return;
    if (!binder) {
        fail(env, "java/lang/IllegalArgumentException",
             "Unsupported Binder object");
        return;
    }
    // markForBinder selects the real remote RPC session for a proxy.
    if (binder->remoteBinder()) {
        {
            std::lock_guard<std::mutex> lock(parcelsMutex);
            localParcels.erase(parcel);
        }
        parcel->markForBinder(binder);
    } else
        parcel->markForRpc(localSession);
}

void writeBinder(JNIEnv *env, jclass, jlong pointer, jobject value)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    auto binder = fromJava(env, value);
    if (env->ExceptionCheck())
        return;
    bool local;
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        local = localParcels.count(parcel);
    }
    status_t status;
    if (local && binder) {
        // A local Parcel roundtrip is not a wire receive. Exercise a genuine
        // loopback exchange to balance RPC references, then retain its result
        // for local reads instead of pretending the same endpoint received it.
        Parcel request, reply;
        request.markForRpc(localSession);
        status = request.writeStrongBinder(binder);
        if (status == OK)
            status = localRoot->transact(IBinder::FIRST_CALL_TRANSACTION,
                                         request, &reply);
        sp<IBinder> returned;
        if (status == OK)
            status = reply.readNullableStrongBinder(&returned);
        if (status == OK && returned != binder)
            status = BAD_TYPE;
        // Drain the reference acknowledgements on the outgoing connection.
        // Otherwise an idle sender can retain sentRef until its next call.
        if (status == OK)
            status = localRoot->pingBinder();
        if (status == OK) {
            size_t start = parcel->dataPosition();
            status = parcel->write(reply.data(), reply.dataSize());
            if (status == OK) {
                LocalObject object{
                    returned,
                    std::vector<uint8_t>(reply.data(),
                                         reply.data() + reply.dataSize())};
                std::lock_guard<std::mutex> lock(parcelsMutex);
                auto &objects = localParcels[parcel];
                size_t end = parcel->dataPosition();
                for (auto i = objects.begin(); i != objects.end();) {
                    if (i->first < end &&
                        i->first + i->second.encoding.size() > start)
                        i = objects.erase(i);
                    else
                        ++i;
                }
                objects.emplace(start, std::move(object));
            }
        }
    } else {
        status = parcel->writeStrongBinder(binder);
    }
    if (status != OK)
        fail(env, "java/lang/IllegalStateException",
             "Binder RPC object write failed");
}

jobject readBinder(JNIEnv *env, jclass, jlong pointer)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    sp<IBinder> binder;
    bool found = false;
    bool localParcel = false;
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        auto local = localParcels.find(parcel);
        if (local != localParcels.end()) {
            localParcel = true;
            auto item = local->second.find(parcel->dataPosition());
            if (item != local->second.end()) {
                size_t start = item->first, size = item->second.encoding.size();
                if (start > parcel->dataSize() ||
                    size > parcel->dataSize() - start ||
                    std::memcmp(parcel->data() + start,
                                item->second.encoding.data(), size)) {
                    fail(env, "android/os/BadParcelableException",
                         "Local Binder object was overwritten");
                    return nullptr;
                }
                binder = item->second.binder;
                parcel->setDataPosition(start + size);
                found = true;
            }
        }
    }
    if (!found) {
        if (localParcel) {
            size_t position = parcel->dataPosition();
            int32_t type;
            status_t status = parcel->readInt32(&type);
            parcel->setDataPosition(position);
            if (status != OK || type != 0) {
                fail(env, "android/os/BadParcelableException",
                     "Local Binder object has no owning reference");
                return nullptr;
            }
        }
        status_t status = parcel->readNullableStrongBinder(&binder);
        if (status != OK) {
            fail(env, "android/os/BadParcelableException",
                 "Binder RPC object read failed");
            return nullptr;
        }
    }
    return toJava(env, binder);
}

jboolean hasBinders(JNIEnv *env,
                    jclass,
                    jlong pointer,
                    jint offset,
                    jint length)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    if (offset < 0 || length < 0 ||
        static_cast<size_t>(offset) > parcel->dataSize() ||
        static_cast<size_t>(length) > parcel->dataSize() - offset) {
        fail(env, "java/lang/IllegalArgumentException", "Invalid Parcel range");
        return false;
    }
    std::lock_guard<std::mutex> lock(parcelsMutex);
    auto entry = localParcels.find(parcel);
    if (entry != localParcels.end()) {
        for (const auto &[position, object] : entry->second) {
            if (position < static_cast<size_t>(offset) + length &&
                position + object.encoding.size() > static_cast<size_t>(offset))
                return true;
        }
    }
    bool result = false;
    return parcel->hasBindersInRange(offset, length, &result) == OK && result;
}
jboolean hasAnyBinders(JNIEnv *env, jclass cls, jlong pointer)
{
    return hasBinders(env, cls, pointer, 0,
                      reinterpret_cast<Parcel *>(pointer)->dataSize());
}

jbyteArray marshall(JNIEnv *env, jclass cls, jlong pointer)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    if (hasAnyBinders(env, cls, pointer) || parcel->hasFileDescriptors()) {
        fail(env, "java/lang/RuntimeException",
             "Cannot marshall a Parcel containing objects");
        return nullptr;
    }
    jbyteArray bytes = env->NewByteArray(parcel->dataSize());
    if (bytes)
        env->SetByteArrayRegion(
            bytes, 0, parcel->dataSize(),
            reinterpret_cast<const jbyte *>(parcel->data()));
    return bytes;
}

void unmarshall(JNIEnv *env,
                jclass cls,
                jlong pointer,
                jbyteArray bytes,
                jint offset,
                jint length)
{
    if (!bytes || offset < 0 || length < 0 ||
        offset > env->GetArrayLength(bytes) ||
        length > env->GetArrayLength(bytes) - offset) {
        fail(env, "java/lang/IndexOutOfBoundsException",
             "Invalid Parcel byte range");
        return;
    }
    std::vector<jbyte> copy(length);
    env->GetByteArrayRegion(bytes, offset, length, copy.data());
    if (env->ExceptionCheck())
        return;
    recycleParcel(env, cls, pointer);
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    if (parcel->setData(reinterpret_cast<const uint8_t *>(copy.data()),
                        length) != OK) {
        fail(env, "java/lang/OutOfMemoryError", "Parcel allocation failed");
    }
}

void setDataSize(JNIEnv *env, jclass, jlong pointer, jint size)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    if (size < 0 || parcel->setDataSize(size) != OK) {
        fail(env, "java/lang/IllegalArgumentException", "Invalid Parcel size");
        return;
    }
    std::lock_guard<std::mutex> lock(parcelsMutex);
    auto entry = localParcels.find(parcel);
    if (entry != localParcels.end()) {
        for (auto i = entry->second.begin(); i != entry->second.end();) {
            if (i->first + i->second.encoding.size() >
                static_cast<size_t>(size))
                i = entry->second.erase(i);
            else
                ++i;
        }
    }
}

void appendFrom(JNIEnv *env,
                jclass,
                jlong pointer,
                jlong otherPointer,
                jint offset,
                jint length)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    auto other = reinterpret_cast<Parcel *>(otherPointer);
    if (offset < 0 || length < 0 ||
        static_cast<size_t>(offset) > other->dataSize() ||
        static_cast<size_t>(length) > other->dataSize() - offset) {
        fail(env, "java/lang/IllegalArgumentException",
             "Invalid Parcel append range");
        return;
    }
    size_t start = parcel->dataPosition();
    std::map<size_t, LocalObject> copied;
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        auto source = localParcels.find(other);
        if (source != localParcels.end()) {
            for (const auto &[position, object] : source->second) {
                size_t end = position + object.encoding.size();
                if (position < static_cast<size_t>(offset) + length &&
                    end > static_cast<size_t>(offset)) {
                    if (position < static_cast<size_t>(offset) ||
                        end > static_cast<size_t>(offset) + length) {
                        fail(env, "android/os/BadParcelableException",
                             "Partial Binder object append");
                        return;
                    }
                    copied.emplace(start + position - offset, object);
                }
            }
            if (!copied.empty() && !localParcels.count(parcel)) {
                fail(env, "java/lang/UnsupportedOperationException",
                     "Cannot append local Binder objects into a remote parcel");
                return;
            }
        }
    }
    if (parcel->appendFrom(other, offset, length) != OK) {
        fail(env, "android/os/BadParcelableException", "Parcel append failed");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        auto destination = localParcels.find(parcel);
        if (destination != localParcels.end()) {
            auto &objects = destination->second;
            for (auto i = objects.begin(); i != objects.end();) {
                if (i->first < start + length &&
                    i->first + i->second.encoding.size() > start)
                    i = objects.erase(i);
                else
                    ++i;
            }
            for (auto &[position, object] : copied)
                objects[position] = std::move(object);
        }
    }
}

void enforceInterface(JNIEnv *env, jclass, jlong pointer, jstring name)
{
    auto parcel = reinterpret_cast<Parcel *>(pointer);
    if (!parcel->isForRpc()) {
        fail(env, "java/lang/UnsupportedOperationException",
             "Kernel Binder unavailable");
        return;
    }
    const jchar *chars = env->GetStringChars(name, nullptr);
    if (!chars)
        return;
    bool valid =
        parcel->enforceInterface(reinterpret_cast<const char16_t *>(chars),
                                 env->GetStringLength(name), nullptr);
    env->ReleaseStringChars(name, chars);
    if (!valid)
        fail(env, "java/lang/SecurityException",
             "Binder interface descriptor mismatch");
}

class JavaDeathRecipient final : public IBinder::DeathRecipient
{
public:
    jobject object;
    wp<IBinder> target;
    JavaDeathRecipient(JNIEnv *env, jobject value, const sp<IBinder> &binder)
        : object(env->NewGlobalRef(value)), target(binder)
    {
    }
    ~JavaDeathRecipient() override
    {
        bool attached;
        JNIEnv *env = environment(&attached);
        if (env)
            env->DeleteGlobalRef(object);
        if (attached && env)
            vm->DetachCurrentThread();
    }
    void binderDied(const wp<IBinder> &) override;
};
std::mutex deathsMutex;
std::map<IBinder *, std::vector<sp<JavaDeathRecipient>>> deaths;

void JavaDeathRecipient::binderDied(const wp<IBinder> &who)
{
    sp<JavaDeathRecipient> owned = sp<JavaDeathRecipient>::fromExisting(this);
    bool attached;
    JNIEnv *env = environment(&attached);
    if (env) {
        env->CallVoidMethod(object, deathCallback);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }
    {
        std::lock_guard<std::mutex> lock(deathsMutex);
        auto entry = deaths.find(who.unsafe_get());
        if (entry != deaths.end()) {
            auto &list = entry->second;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [this](const auto &item) {
                                          return item.get() == this;
                                      }),
                       list.end());
            if (list.empty())
                deaths.erase(entry);
        }
    }
    if (attached && env)
        vm->DetachCurrentThread();
}

void linkRemote(JNIEnv *env,
                jclass,
                jlong pointer,
                jobject recipient,
                jint flags)
{
    auto holder = reinterpret_cast<sp<IBinder> *>(pointer);
    if (!holder || !*holder) {
        fail(env, "android/os/DeadObjectException", "Released RPC binder");
        return;
    }
    auto callback = sp<JavaDeathRecipient>::make(env, recipient, *holder);
    {
        std::lock_guard<std::mutex> lock(deathsMutex);
        deaths[holder->get()].push_back(callback);
    }
    status_t status = (*holder)->linkToDeath(callback, nullptr, flags);
    if (status != OK) {
        std::lock_guard<std::mutex> lock(deathsMutex);
        auto &list = deaths[holder->get()];
        list.erase(std::remove(list.begin(), list.end(), callback), list.end());
        if (list.empty())
            deaths.erase(holder->get());
        fail(env,
             status == DEAD_OBJECT ? "android/os/DeadObjectException"
                                   : "android/os/RemoteException",
             "RPC death registration failed");
    }
}

jboolean unlinkRemote(JNIEnv *env,
                      jclass,
                      jlong pointer,
                      jobject recipient,
                      jint flags)
{
    auto holder = reinterpret_cast<sp<IBinder> *>(pointer);
    if (!holder || !*holder)
        return false;
    sp<JavaDeathRecipient> found;
    {
        std::lock_guard<std::mutex> lock(deathsMutex);
        auto entry = deaths.find(holder->get());
        if (entry != deaths.end()) {
            auto &list = entry->second;
            for (auto item = list.begin(); item != list.end(); ++item) {
                if (env->IsSameObject((*item)->object, recipient)) {
                    found = *item;
                    list.erase(item);
                    break;
                }
            }
            if (list.empty())
                deaths.erase(entry);
        }
    }
    if (!found)
        return false;
    status_t status = (*holder)->unlinkToDeath(found, nullptr, flags);
    return status == OK;
}

jlong startServer(JNIEnv *env, jclass, jstring socket, jobject root)
{
    if (!socket || !root) {
        fail(env, "java/lang/NullPointerException",
             "socket and root are required");
        return 0;
    }
    auto binder = fromJava(env, root);
    if (!binder || env->ExceptionCheck())
        return 0;
    const char *path = env->GetStringUTFChars(socket, nullptr);
    if (!path)
        return 0;
    auto server = RpcServer::make();
    server->setMaxThreads(4);
    server->setRootObject(binder);
    status_t status = server->setupUnixDomainServer(path);
    if (status == OK && chmod(path, 0600) != 0)
        status = -errno;
    env->ReleaseStringUTFChars(socket, path);
    if (status != OK) {
        fail(env, "android/os/RemoteException", "RPC socket listen failed");
        return 0;
    }
    server->start();
    std::lock_guard<std::mutex> lock(serversMutex);
    jlong handle = nextServer++;
    servers.emplace(handle, server);
    return handle;
}

jobject connectServer(JNIEnv *env, jclass, jstring socket)
{
    if (!socket) {
        fail(env, "java/lang/NullPointerException", "socket is required");
        return nullptr;
    }
    const char *path = env->GetStringUTFChars(socket, nullptr);
    if (!path)
        return nullptr;
    auto session = RpcSession::make();
    session->setMaxIncomingThreads(2);
    status_t status = session->setupUnixDomainClient(path);
    env->ReleaseStringUTFChars(socket, path);
    if (status != OK) {
        fprintf(stderr, "[Muplar/BinderRPC] connect status=%d errno=%d\n",
                status, errno);
        fail(env, "android/os/RemoteException", "RPC socket connection failed");
        return nullptr;
    }
    return toJava(env, session->getRootObject());
}

void stopServer(JNIEnv *env, jclass, jlong handle)
{
    sp<RpcServer> server;
    {
        std::lock_guard<std::mutex> lock(serversMutex);
        auto entry = servers.find(handle);
        if (entry == servers.end()) {
            fail(env, "java/lang/IllegalArgumentException",
                 "Unknown RPC server handle");
            return;
        }
        server = entry->second;
        servers.erase(entry);
    }
    if (!server->shutdown())
        fail(env, "java/lang/IllegalStateException",
             "RPC server shutdown failed");
}

void releaseRemote(JNIEnv *env, jclass, jlong pointer)
{
    auto holder = reinterpret_cast<sp<IBinder> *>(pointer);
    if (!holder)
        return;
    {
        std::lock_guard<std::mutex> lock(remoteProxiesMutex);
        auto entry = remoteProxies.find(holder->get());
        if (entry != remoteProxies.end() && entry->second.handle == pointer) {
            env->DeleteWeakGlobalRef(entry->second.object);
            remoteProxies.erase(entry);
        }
    }
    delete holder;
}

jboolean remoteTransact(JNIEnv *env,
                        jclass,
                        jlong pointer,
                        jint code,
                        jobject data,
                        jobject reply,
                        jint flags)
{
    auto binder = reinterpret_cast<sp<IBinder> *>(pointer);
    if (!binder || !*binder) {
        fail(env, "android/os/DeadObjectException", "Released RPC binder");
        return false;
    }
    auto input =
        reinterpret_cast<Parcel *>(env->GetLongField(data, parcelPointer));
    auto output = reply ? reinterpret_cast<Parcel *>(
                              env->GetLongField(reply, parcelPointer))
                        : nullptr;
    Parcel routed;
    std::map<size_t, LocalObject> objects;
    bool local;
    {
        std::lock_guard<std::mutex> lock(parcelsMutex);
        auto entry = localParcels.find(input);
        local = entry != localParcels.end();
        if (local)
            objects = entry->second;
        if (output)
            localParcels.erase(output);
    }
    if (local) {
        if (input->hasFileDescriptors()) {
            fail(env, "java/lang/UnsupportedOperationException",
                 "Local-to-RPC descriptor transfer is not implemented");
            return false;
        }
        routed.markForBinder(*binder);
        size_t previous = 0;
        for (const auto &[offset, object] : objects) {
            size_t size = object.encoding.size();
            if (offset < previous || offset > input->dataSize() ||
                size > input->dataSize() - offset ||
                std::memcmp(input->data() + offset, object.encoding.data(),
                            size)) {
                fail(env, "android/os/BadParcelableException",
                     "Local Binder object was overwritten");
                return false;
            }
            if (routed.write(input->data() + previous, offset - previous) !=
                    OK ||
                routed.writeStrongBinder(object.binder) != OK) {
                fail(env, "android/os/RemoteException",
                     "RPC parcel conversion failed");
                return false;
            }
            previous = offset + size;
        }
        if (routed.write(input->data() + previous,
                         input->dataSize() - previous) != OK)
            return false;
    }
    status_t result =
        (*binder)->transact(code, local ? routed : *input, output, flags);
    if (result == UNKNOWN_TRANSACTION)
        return false;
    if (result != OK) {
        fail(env,
             result == DEAD_OBJECT ? "android/os/DeadObjectException"
                                   : "android/os/RemoteException",
             "Binder RPC transaction failed");
        return false;
    }
    if (output)
        output->setDataPosition(0);
    return true;
}

jboolean remoteAlive(JNIEnv *, jclass, jlong pointer, jboolean ping)
{
    auto binder = reinterpret_cast<sp<IBinder> *>(pointer);
    return binder && *binder &&
           (ping ? (*binder)->pingBinder() == OK : (*binder)->isBinderAlive());
}

jstring remoteDescriptor(JNIEnv *env, jclass, jlong pointer)
{
    auto binder = reinterpret_cast<sp<IBinder> *>(pointer);
    if (!binder || !*binder)
        return nullptr;
    const String16 &name = (*binder)->getInterfaceDescriptor();
    return env->NewString(reinterpret_cast<const jchar *>(name.c_str()),
                          name.size());
}

bool registerMethod(JNIEnv *env,
                    jclass cls,
                    const char *name,
                    const char *signature,
                    void *function)
{
    JNINativeMethod method{const_cast<char *>(name),
                           const_cast<char *>(signature), function};
    return env->RegisterNatives(cls, &method, 1) == JNI_OK;
}
}  // namespace

extern "C" int muplar_binder_rpc_register(JNIEnv *env)
{
    env->GetJavaVM(&vm);
    parcelClass = static_cast<jclass>(
        env->NewGlobalRef(env->FindClass("android/os/Parcel")));
    binderClass = static_cast<jclass>(
        env->NewGlobalRef(env->FindClass("android/os/Binder")));
    if (!parcelClass || !binderClass || env->ExceptionCheck())
        return -1;
    parcelObtain =
        env->GetStaticMethodID(parcelClass, "obtain", "(J)Landroid/os/Parcel;");
    parcelRecycle = env->GetMethodID(parcelClass, "recycle", "()V");
    remoteClass = static_cast<jclass>(env->NewGlobalRef(
        env->FindClass("com/muplar/runtime/MuplarRemoteBinder")));
    if (!remoteClass || env->ExceptionCheck())
        return -1;
    remoteHandle = env->GetFieldID(remoteClass, "handle", "J");
    remoteConstructor = env->GetMethodID(remoteClass, "<init>", "(J)V");
    jclass deathClass = env->FindClass("android/os/IBinder$DeathRecipient");
    if (!deathClass || env->ExceptionCheck())
        return -1;
    deathCallback = env->GetMethodID(deathClass, "binderDied", "()V");
    parcelPointer = env->GetFieldID(parcelClass, "mNativePtr", "J");
    exceptionClass = static_cast<jclass>(
        env->NewGlobalRef(env->FindClass("java/lang/Exception")));
    parcelWriteException = env->GetMethodID(parcelClass, "writeException",
                                            "(Ljava/lang/Exception;)V");
    binderDescriptor = env->GetMethodID(binderClass, "getInterfaceDescriptor",
                                        "()Ljava/lang/String;");
    binderTransact = env->GetMethodID(
        binderClass, "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z");
    if (env->ExceptionCheck())
        return -1;
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) < 0)
        return -1;
    localServer = RpcServer::make();
    localServer->setMaxThreads(1);
    localServer->setRootObject(sp<LocalRelay>::make());
    if (localServer->setupUnixDomainSocketBootstrapServer(
            binder::unique_fd(sockets[0])) != OK) {
        close(sockets[1]);
        return -1;
    }
    localServer->start();
    localSession = RpcSession::make();
    localSession->setMaxIncomingThreads(0);
    if (localSession->setupUnixDomainSocketBootstrapClient(
            binder::unique_fd(sockets[1])) != OK)
        return -1;
    localRoot = localSession->getRootObject();
    if (!localRoot)
        return -1;
    if (!registerMethod(env, parcelClass, "nativeHasBinders", "(J)Z",
                        reinterpret_cast<void *>(hasAnyBinders)) ||
        !registerMethod(env, parcelClass, "nativeHasBindersInRange", "(JII)Z",
                        reinterpret_cast<void *>(hasBinders)) ||
        !registerMethod(env, parcelClass, "nativeMarshall", "(J)[B",
                        reinterpret_cast<void *>(marshall)) ||
        !registerMethod(env, parcelClass, "nativeUnmarshall", "(J[BII)V",
                        reinterpret_cast<void *>(unmarshall)) ||
        !registerMethod(env, parcelClass, "nativeSetDataSize", "(JI)V",
                        reinterpret_cast<void *>(setDataSize)) ||
        !registerMethod(env, parcelClass, "nativeAppendFrom", "(JJII)V",
                        reinterpret_cast<void *>(appendFrom)) ||
        !registerMethod(env, parcelClass, "nativeDestroy", "(J)V",
                        reinterpret_cast<void *>(destroyParcel)) ||
        !registerMethod(env, parcelClass, "nativeFreeBuffer", "(J)V",
                        reinterpret_cast<void *>(recycleParcel)) ||
        !registerMethod(env, parcelClass, "nativeCreate", "()J",
                        reinterpret_cast<void *>(createParcel)) ||
        !registerMethod(env, parcelClass, "nativeMarkForBinder",
                        "(JLandroid/os/IBinder;)V",
                        reinterpret_cast<void *>(markForBinder)) ||
        !registerMethod(env, parcelClass, "nativeWriteStrongBinder",
                        "(JLandroid/os/IBinder;)V",
                        reinterpret_cast<void *>(writeBinder)) ||
        !registerMethod(env, parcelClass, "nativeReadStrongBinder",
                        "(J)Landroid/os/IBinder;",
                        reinterpret_cast<void *>(readBinder)) ||
        !registerMethod(env, parcelClass, "nativeEnforceInterface",
                        "(JLjava/lang/String;)V",
                        reinterpret_cast<void *>(enforceInterface)))
        return -1;
    if (!registerMethod(env, remoteClass, "release", "(J)V",
                        reinterpret_cast<void *>(releaseRemote)) ||
        !registerMethod(env, remoteClass, "transact",
                        "(JILandroid/os/Parcel;Landroid/os/Parcel;I)Z",
                        reinterpret_cast<void *>(remoteTransact)) ||
        !registerMethod(env, remoteClass, "alive", "(JZ)Z",
                        reinterpret_cast<void *>(remoteAlive)) ||
        !registerMethod(env, remoteClass, "descriptor", "(J)Ljava/lang/String;",
                        reinterpret_cast<void *>(remoteDescriptor)))
        return -1;
    if (!registerMethod(env, remoteClass, "link",
                        "(JLandroid/os/IBinder$DeathRecipient;I)V",
                        reinterpret_cast<void *>(linkRemote)) ||
        !registerMethod(env, remoteClass, "unlink",
                        "(JLandroid/os/IBinder$DeathRecipient;I)Z",
                        reinterpret_cast<void *>(unlinkRemote)))
        return -1;
    jclass api = env->FindClass("com/muplar/runtime/MuplarBinderRpc");
    if (!api ||
        !registerMethod(env, api, "startServer",
                        "(Ljava/lang/String;Landroid/os/IBinder;)J",
                        reinterpret_cast<void *>(startServer)) ||
        !registerMethod(env, api, "connect",
                        "(Ljava/lang/String;)Landroid/os/IBinder;",
                        reinterpret_cast<void *>(connectServer)) ||
        !registerMethod(env, api, "stopServer", "(J)V",
                        reinterpret_cast<void *>(stopServer)))
        return -1;
    fprintf(stderr, "[Muplar/BinderRPC] native socket transport initialized\n");
    return 0;
}
