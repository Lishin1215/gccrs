// { dg-additional-options "-frust-borrowcheck -w" }
// { dg-output "drop 7\r*\ndrop 8\r*\n" }
#![feature(no_core, lang_items)]
#![no_core]

extern "C" {
    fn printf(s: *const i8, ...);
}

#[lang = "sized"]
pub trait Sized {}

#[lang = "copy"]
trait Copy {}

#[lang = "drop"]
trait Drop {
    fn drop(&mut self);
}

struct NeedsDrop {
    value: i32,
}

impl Drop for NeedsDrop {
    fn drop(&mut self) {
        unsafe {
            printf("drop %i\n\0" as *const str as *const i8, self.value);
        }
    }
}

fn main() -> i32 {
    {
        let x = NeedsDrop { value: 7 };
        let _y = x;
    }

    let _z = NeedsDrop { value: 8 };
    0
}
