fn main() {
    println!("cargo:rustc-link-arg=-Wl,-u,slint_platform_register");
    println!("cargo:rustc-link-arg=-Wl,-u,slint_ensure_backend");
    println!("cargo:rustc-link-arg=-Wl,--export-dynamic");
}
