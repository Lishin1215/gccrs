// { dg-output "a\r*\nu\r*\na\r*\nb\r*\na\r*\nl\r*\np\r*\n" }
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
struct B;
struct Local;
struct Param;

impl Drop for A {
    fn drop(&mut self) {
        unsafe {
            printf("a\n\0" as *const str as *const i8);
        }
    }
}

impl Drop for B {
    fn drop(&mut self) {
        unsafe {
            printf("b\n\0" as *const str as *const i8);
        }
    }
}

impl Drop for Local {
    fn drop(&mut self) {
        unsafe {
            printf("l\n\0" as *const str as *const i8);
        }
    }
}

impl Drop for Param {
    fn drop(&mut self) {
        unsafe {
            printf("p\n\0" as *const str as *const i8);
        }
    }
}

fn return_unit() {
    let _a = A;
    return;
}

fn make_unit() {
    unsafe {
        printf("u\n\0" as *const str as *const i8);
    }
}

fn return_unit_value() {
    let _a = A;
    return make_unit();
}

fn return_nested() {
    let _a = A;
    {
        let _b = B;
        return;
    }
}

fn return_with_param(_param: Param) {
    let _local = Local;
    return;
}

fn main() -> i32 {
    return_unit();
    return_unit_value();
    return_nested();
    return_with_param(Param);
    0
}
