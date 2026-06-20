// { dg-output "v\r*\na\r*\n" }
// { dg-additional-options "-w" }
#![feature(no_core)]
#![feature(lang_items)]
#![no_core]

extern "C" {
    fn printf(s: *const i8, ...);
}

#[lang = "sized"]
pub trait Sized {}

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
}

struct A;

impl Drop for A {
    fn drop(&mut self) {
        unsafe {
            printf("a\n\0" as *const str as *const i8);
        }
    }
}

fn make_value() -> i32 {
    unsafe {
        printf("v\n\0" as *const str as *const i8);
    }
    0
}

fn return_value() -> i32 {
    let _a = A;
    return make_value();
}

fn main() -> i32 {
    return_value()
}
