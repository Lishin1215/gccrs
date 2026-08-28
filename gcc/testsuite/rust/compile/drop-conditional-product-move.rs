// { dg-additional-options "-frust-borrowcheck -w" }

#![feature(no_core)]
#![feature(lang_items)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
}

struct Droppable;

impl Drop for Droppable {
    fn drop(&mut self) {}
}

struct Pair {
    first: Droppable,
    second: Droppable,
}

fn unconditional_product_move() {
    let first = Droppable; // { dg-error "use of moved value" }
    let second = Droppable; // { dg-error "use of moved value" }
    let _pair = Pair { first, second }; // { dg-message "sorry, unimplemented: moving multiple IDs within the same location is not yet supported" }
}

fn conditional_product_move(condition: bool) {
    let first = Droppable; // { dg-error "use of moved value" }
    let second = Droppable; // { dg-error "use of moved value" }

    if condition {
        let _pair = Pair {
            first,
            second, // { dg-message "sorry, unimplemented: moving multiple IDs within the same location is not yet supported" }
        };
    }
}

fn main() {
    unconditional_product_move();
    conditional_product_move(true);
}
