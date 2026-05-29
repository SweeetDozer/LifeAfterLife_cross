#[cfg(target_os = "android")]
unsafe extern "C" {
    fn lal_android_main() -> i32;
    fn lal_set_java_vm(vm: *mut core::ffi::c_void);
}

#[cfg(target_os = "android")]
#[no_mangle]
pub fn android_main(app: slint_cpp::android::AndroidApp) {
    unsafe {
        lal_set_java_vm(app.vm_as_ptr());
    }

    slint_cpp::android::init(app).expect("failed to initialize Slint Android backend");

    unsafe {
        let _ = lal_android_main();
    }
}
