#include "android_support.h"

#ifdef __ANDROID__

#include <android/log.h>
#include <jni.h>
#include <array>
#include <string_view>

namespace platform::android {

static JavaVM* g_java_vm = nullptr;

extern "C" void lal_set_java_vm(void* vm) {
    g_java_vm = reinterpret_cast<JavaVM*>(vm);
}

namespace {

class ScopedEnv {
public:
    static std::optional<ScopedEnv> create(std::string& error_message)
    {
        JavaVM* vm = g_java_vm;
        if (vm == nullptr) {
            error_message = "JavaVM not initialized.";
            return std::nullopt;
        }

        JNIEnv* env = nullptr;
        bool detach_on_destroy = false;
        const jint get_env_result = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (get_env_result == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) {
                error_message = "Could not attach the current thread to the Android Java VM.";
                return std::nullopt;
            }
            detach_on_destroy = true;
        } else if (get_env_result != JNI_OK || env == nullptr) {
            error_message = "Could not obtain a JNI environment from the Android Java VM.";
            return std::nullopt;
        }

        return ScopedEnv(vm, env, detach_on_destroy);
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    ScopedEnv(ScopedEnv&& other) noexcept
        : vm_(other.vm_)
        , env_(other.env_)
        , detach_on_destroy_(other.detach_on_destroy_)
    {
        other.vm_ = nullptr;
        other.env_ = nullptr;
        other.detach_on_destroy_ = false;
    }

    ~ScopedEnv()
    {
        if (detach_on_destroy_ && vm_ != nullptr) {
            vm_->DetachCurrentThread();
        }
    }

    JNIEnv* get() const { return env_; }

private:
    ScopedEnv(JavaVM* vm, JNIEnv* env, bool detach_on_destroy)
        : vm_(vm)
        , env_(env)
        , detach_on_destroy_(detach_on_destroy)
    {
    }

    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool detach_on_destroy_ = false;
};

std::string jstring_to_utf8(JNIEnv* env, jstring value)
{
    if (value == nullptr) {
        return {};
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return {};
    }

    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

bool consume_exception(JNIEnv* env, std::string& error_message, std::string_view fallback_message)
{
    if (!env->ExceptionCheck()) {
        return false;
    }

    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    if (throwable != nullptr) {
        env->DeleteLocalRef(throwable);
    }

    error_message = std::string(fallback_message);
    return true;
}

template <typename T>
bool failed_jni_result(JNIEnv* env, T value, std::string& error_message, std::string_view fallback_message)
{
    if (consume_exception(env, error_message, fallback_message)) {
        __android_log_print(ANDROID_LOG_ERROR, "LAL.AndroidHttp", "%s: %s",
                            std::string(fallback_message).c_str(), error_message.c_str());
        return true;
    }

    if (value == nullptr) {
        error_message = std::string(fallback_message);
        __android_log_print(ANDROID_LOG_ERROR, "LAL.AndroidHttp", "%s", error_message.c_str());
        return true;
    }

    return false;
}

jobject current_application(JNIEnv* env, std::string& error_message)
{
    jclass activity_thread_class = env->FindClass("android/app/ActivityThread");
    if (failed_jni_result(env, activity_thread_class, error_message, "Could not find android.app.ActivityThread.")) {
        return nullptr;
    }

    jmethodID current_application_method =
        env->GetStaticMethodID(activity_thread_class, "currentApplication", "()Landroid/app/Application;");
    if (failed_jni_result(env, current_application_method, error_message, "Could not resolve ActivityThread.currentApplication().")) {
        env->DeleteLocalRef(activity_thread_class);
        return nullptr;
    }

    jobject application = env->CallStaticObjectMethod(activity_thread_class, current_application_method);
    if (consume_exception(env, error_message, "Could not obtain the current Android application instance.")) {
        env->DeleteLocalRef(activity_thread_class);
        return nullptr;
    }

    env->DeleteLocalRef(activity_thread_class);
    if (application == nullptr) {
        error_message = "Android application instance is not available yet.";
        return nullptr;
    }

    return application;
}

std::string read_stream_bytes(JNIEnv* env, jobject input_stream, std::string& error_message)
{
    if (input_stream == nullptr) {
        return {};
    }

    jclass input_stream_class = env->FindClass("java/io/InputStream");
    if (failed_jni_result(env, input_stream_class, error_message, "Could not access java.io.InputStream.")) {
        return {};
    }

    jmethodID read_all_bytes_method = env->GetMethodID(input_stream_class, "readAllBytes", "()[B");
    jmethodID close_method = env->GetMethodID(input_stream_class, "close", "()V");
    if (failed_jni_result(env, read_all_bytes_method, error_message, "Could not resolve InputStream.readAllBytes().")
        || failed_jni_result(env, close_method, error_message, "Could not resolve InputStream.close().")) {
        env->DeleteLocalRef(input_stream_class);
        return {};
    }

    auto byte_array = static_cast<jbyteArray>(env->CallObjectMethod(input_stream, read_all_bytes_method));
    if (consume_exception(env, error_message, "Could not read the HTTP response body from Android.")) {
        env->DeleteLocalRef(input_stream_class);
        return {};
    }

    env->CallVoidMethod(input_stream, close_method);
    consume_exception(env, error_message, "Could not close the Android input stream.");

    std::string result;
    if (byte_array != nullptr) {
        const jsize size = env->GetArrayLength(byte_array);
        result.resize(static_cast<std::size_t>(size));
        if (size > 0) {
            env->GetByteArrayRegion(byte_array, 0, size, reinterpret_cast<jbyte*>(result.data()));
        }
        env->DeleteLocalRef(byte_array);
    }

    env->DeleteLocalRef(input_stream_class);
    return result;
}

} // namespace

std::optional<HttpResponse> http_request(const HttpRequest& request, std::string& error_message)
{
    auto scoped_env = ScopedEnv::create(error_message);
    if (!scoped_env) {
        return std::nullopt;
    }

    JNIEnv* env = scoped_env->get();

    jstring url_text = env->NewStringUTF(request.url.c_str());
    jclass url_class = env->FindClass("java/net/URL");
    if (failed_jni_result(env, url_text, error_message, "Could not allocate Android URL string.")
        || failed_jni_result(env, url_class, error_message, "Could not access java.net.URL.")) {
        return std::nullopt;
    }

    jmethodID url_ctor = env->GetMethodID(url_class, "<init>", "(Ljava/lang/String;)V");
    jmethodID open_connection_method = env->GetMethodID(url_class, "openConnection", "()Ljava/net/URLConnection;");
    if (failed_jni_result(env, url_ctor, error_message, "Could not resolve URL constructor.")
        || failed_jni_result(env, open_connection_method, error_message, "Could not resolve URL.openConnection().")) {
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    jobject url_object = env->NewObject(url_class, url_ctor, url_text);
    if (failed_jni_result(env, url_object, error_message, "Could not create a URL object for Android HTTP.")) {
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    jobject connection = env->CallObjectMethod(url_object, open_connection_method);
    if (failed_jni_result(env, connection, error_message, "Could not open an Android HTTP connection.")) {
        env->DeleteLocalRef(url_object);
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    jclass http_connection_class = env->FindClass("java/net/HttpURLConnection");
    if (failed_jni_result(env, http_connection_class, error_message, "Could not access HttpURLConnection.")) {
        env->DeleteLocalRef(connection);
        env->DeleteLocalRef(url_object);
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    const auto set_int_property = [&](const char* name, const char* signature, jint value) -> bool {
        jmethodID method = env->GetMethodID(http_connection_class, name, signature);
        if (failed_jni_result(env, method, error_message, "Could not resolve an HttpURLConnection setter.")) {
            return false;
        }
        env->CallVoidMethod(connection, method, value);
        return !consume_exception(env, error_message, "Could not configure the Android HTTP connection.");
    };

    const auto set_bool_property = [&](const char* name, jboolean value) -> bool {
        jmethodID method = env->GetMethodID(http_connection_class, name, "(Z)V");
        if (failed_jni_result(env, method, error_message, "Could not resolve an HttpURLConnection boolean setter.")) {
            return false;
        }
        env->CallVoidMethod(connection, method, value);
        return !consume_exception(env, error_message, "Could not configure the Android HTTP connection.");
    };

    jmethodID set_request_method = env->GetMethodID(http_connection_class, "setRequestMethod", "(Ljava/lang/String;)V");
    jmethodID set_request_property = env->GetMethodID(http_connection_class, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
    jmethodID get_response_code = env->GetMethodID(http_connection_class, "getResponseCode", "()I");
    jmethodID get_input_stream = env->GetMethodID(http_connection_class, "getInputStream", "()Ljava/io/InputStream;");
    jmethodID get_error_stream = env->GetMethodID(http_connection_class, "getErrorStream", "()Ljava/io/InputStream;");
    jmethodID get_output_stream = env->GetMethodID(http_connection_class, "getOutputStream", "()Ljava/io/OutputStream;");
    jmethodID disconnect_method = env->GetMethodID(http_connection_class, "disconnect", "()V");
    if (failed_jni_result(env, set_request_method, error_message, "Could not resolve HttpURLConnection.setRequestMethod().")
        || failed_jni_result(env, set_request_property, error_message, "Could not resolve HttpURLConnection.setRequestProperty().")
        || failed_jni_result(env, get_response_code, error_message, "Could not resolve HttpURLConnection.getResponseCode().")
        || failed_jni_result(env, get_input_stream, error_message, "Could not resolve HttpURLConnection.getInputStream().")
        || failed_jni_result(env, get_error_stream, error_message, "Could not resolve HttpURLConnection.getErrorStream().")
        || failed_jni_result(env, get_output_stream, error_message, "Could not resolve HttpURLConnection.getOutputStream().")
        || failed_jni_result(env, disconnect_method, error_message, "Could not resolve HttpURLConnection.disconnect().")) {
        env->DeleteLocalRef(http_connection_class);
        env->DeleteLocalRef(connection);
        env->DeleteLocalRef(url_object);
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    jstring method_text = env->NewStringUTF(request.method.c_str());
    if (failed_jni_result(env, method_text, error_message, "Could not allocate Android HTTP method string.")) {
        env->DeleteLocalRef(http_connection_class);
        env->DeleteLocalRef(connection);
        env->DeleteLocalRef(url_object);
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }
    env->CallVoidMethod(connection, set_request_method, method_text);
    env->DeleteLocalRef(method_text);
    if (consume_exception(env, error_message, "Could not set the Android HTTP method.")) {
        env->DeleteLocalRef(http_connection_class);
        env->DeleteLocalRef(connection);
        env->DeleteLocalRef(url_object);
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    if (!set_int_property("setConnectTimeout", "(I)V", request.connect_timeout_ms)
        || !set_int_property("setReadTimeout", "(I)V", request.read_timeout_ms)
        || !set_bool_property("setDoInput", JNI_TRUE)
        || !set_bool_property("setUseCaches", JNI_FALSE)) {
        env->DeleteLocalRef(http_connection_class);
        env->DeleteLocalRef(connection);
        env->DeleteLocalRef(url_object);
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    for (const auto& header : request.headers) {
        jstring header_name = env->NewStringUTF(header.name.c_str());
        jstring header_value = env->NewStringUTF(header.value.c_str());
        if (failed_jni_result(env, header_name, error_message, "Could not allocate Android HTTP header name.")
            || failed_jni_result(env, header_value, error_message, "Could not allocate Android HTTP header value.")) {
            if (header_name != nullptr) {
                env->DeleteLocalRef(header_name);
            }
            if (header_value != nullptr) {
                env->DeleteLocalRef(header_value);
            }
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }
        env->CallVoidMethod(connection, set_request_property, header_name, header_value);
        env->DeleteLocalRef(header_name);
        env->DeleteLocalRef(header_value);
        if (consume_exception(env, error_message, "Could not set an Android HTTP header.")) {
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }
    }

    if (!request.body.empty()) {
        if (!set_bool_property("setDoOutput", JNI_TRUE)) {
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }

        jobject output_stream = env->CallObjectMethod(connection, get_output_stream);
        if (failed_jni_result(env, output_stream, error_message, "Could not open the Android HTTP request body stream.")) {
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }

        jclass output_stream_class = env->FindClass("java/io/OutputStream");
        if (failed_jni_result(env, output_stream_class, error_message, "Could not access java.io.OutputStream.")) {
            env->DeleteLocalRef(output_stream);
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }
        jmethodID write_method = env->GetMethodID(output_stream_class, "write", "([B)V");
        jmethodID close_method = env->GetMethodID(output_stream_class, "close", "()V");
        if (failed_jni_result(env, write_method, error_message, "Could not resolve OutputStream.write().")
            || failed_jni_result(env, close_method, error_message, "Could not resolve OutputStream.close().")) {
            env->DeleteLocalRef(output_stream);
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }

        jbyteArray bytes = env->NewByteArray(static_cast<jsize>(request.body.size()));
        if (bytes == nullptr) {
            error_message = "Could not allocate a Java byte array for the Android HTTP request body.";
            env->DeleteLocalRef(output_stream_class);
            env->DeleteLocalRef(output_stream);
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }

        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(request.body.size()),
                                reinterpret_cast<const jbyte*>(request.body.data()));
        env->CallVoidMethod(output_stream, write_method, bytes);
        if (consume_exception(env, error_message, "Could not write the Android HTTP request body.")) {
            env->DeleteLocalRef(bytes);
            env->DeleteLocalRef(output_stream_class);
            env->DeleteLocalRef(output_stream);
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }

        env->CallVoidMethod(output_stream, close_method);
        consume_exception(env, error_message, "Could not close the Android HTTP request body stream.");

        env->DeleteLocalRef(bytes);
        env->DeleteLocalRef(output_stream_class);
        env->DeleteLocalRef(output_stream);
    }

    const jint status_code = env->CallIntMethod(connection, get_response_code);
    if (consume_exception(env, error_message, "Could not get the Android HTTP response code.")) {
        env->DeleteLocalRef(http_connection_class);
        env->DeleteLocalRef(connection);
        env->DeleteLocalRef(url_object);
        env->DeleteLocalRef(url_text);
        env->DeleteLocalRef(url_class);
        return std::nullopt;
    }

    jobject input_stream = nullptr;
    if (status_code >= 400) {
        input_stream = env->CallObjectMethod(connection, get_error_stream);
        consume_exception(env, error_message, "Could not open the Android HTTP error stream.");
    } else {
        input_stream = env->CallObjectMethod(connection, get_input_stream);
        if (consume_exception(env, error_message, "Could not open the Android HTTP response stream.")) {
            env->DeleteLocalRef(http_connection_class);
            env->DeleteLocalRef(connection);
            env->DeleteLocalRef(url_object);
            env->DeleteLocalRef(url_text);
            env->DeleteLocalRef(url_class);
            return std::nullopt;
        }
    }

    std::string response_body = read_stream_bytes(env, input_stream, error_message);
    if (input_stream != nullptr) {
        env->DeleteLocalRef(input_stream);
    }

    env->CallVoidMethod(connection, disconnect_method);
    consume_exception(env, error_message, "Could not disconnect the Android HTTP connection cleanly.");

    env->DeleteLocalRef(http_connection_class);
    env->DeleteLocalRef(connection);
    env->DeleteLocalRef(url_object);
    env->DeleteLocalRef(url_text);
    env->DeleteLocalRef(url_class);

    return HttpResponse {
        .status_code = static_cast<int>(status_code),
        .body = std::move(response_body),
    };
}

std::optional<std::filesystem::path> files_directory(std::string& error_message)
{
    auto scoped_env = ScopedEnv::create(error_message);
    if (!scoped_env) {
        return std::nullopt;
    }

    JNIEnv* env = scoped_env->get();
    jobject application = current_application(env, error_message);
    if (application == nullptr) {
        return std::nullopt;
    }

    jclass context_class = env->FindClass("android/content/Context");
    if (failed_jni_result(env, context_class, error_message, "Could not access android.content.Context.")) {
        env->DeleteLocalRef(application);
        return std::nullopt;
    }

    jmethodID get_files_dir = env->GetMethodID(context_class, "getFilesDir", "()Ljava/io/File;");
    if (failed_jni_result(env, get_files_dir, error_message, "Could not resolve Context.getFilesDir().")) {
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(application);
        return std::nullopt;
    }

    jobject file_object = env->CallObjectMethod(application, get_files_dir);
    if (failed_jni_result(env, file_object, error_message, "Could not obtain the Android files directory.")) {
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(application);
        return std::nullopt;
    }

    jclass file_class = env->FindClass("java/io/File");
    if (failed_jni_result(env, file_class, error_message, "Could not access java.io.File.")) {
        env->DeleteLocalRef(file_object);
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(application);
        return std::nullopt;
    }
    jmethodID get_absolute_path = env->GetMethodID(file_class, "getAbsolutePath", "()Ljava/lang/String;");
    if (failed_jni_result(env, get_absolute_path, error_message, "Could not resolve java.io.File.getAbsolutePath().")) {
        env->DeleteLocalRef(file_object);
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(application);
        return std::nullopt;
    }

    auto path_text = static_cast<jstring>(env->CallObjectMethod(file_object, get_absolute_path));
    if (failed_jni_result(env, path_text, error_message, "Could not get the Android files directory path.")) {
        env->DeleteLocalRef(file_class);
        env->DeleteLocalRef(file_object);
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(application);
        return std::nullopt;
    }

    const std::string utf8_path = jstring_to_utf8(env, path_text);
    env->DeleteLocalRef(path_text);
    env->DeleteLocalRef(file_class);
    env->DeleteLocalRef(file_object);
    env->DeleteLocalRef(context_class);
    env->DeleteLocalRef(application);

    if (utf8_path.empty()) {
        error_message = "Android returned an empty files directory path.";
        return std::nullopt;
    }

    return std::filesystem::path(utf8_path);
}

} // namespace platform::android

#else

namespace platform::android {

std::optional<HttpResponse> http_request(const HttpRequest&, std::string& error_message)
{
    error_message = "Android support is not available on this platform.";
    return std::nullopt;
}

std::optional<std::filesystem::path> files_directory(std::string& error_message)
{
    error_message = "Android support is not available on this platform.";
    return std::nullopt;
}

} // namespace platform::android

#endif
