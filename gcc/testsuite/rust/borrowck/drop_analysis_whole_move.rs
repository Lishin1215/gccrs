// { dg-additional-options "-frust-compile-until=compilation -frust-borrowcheck -frust-dump-bir" }
// { dg-final { scan-file bir_dump/drop_analysis_whole_move.whole_move.bir.dump "drop-state x: dead" } }
// { dg-final { scan-file bir_dump/drop_analysis_whole_move.whole_move.bir.dump "drop-state y: static" } }
// { dg-final { scan-file bir_dump/drop_analysis_whole_move.copy.bir.dump "drop-state x: static" } }
// { dg-final { scan-file bir_dump/drop_analysis_whole_move.copy.bir.dump "drop-state y: static" } }
// { dg-final { scan-file bir_dump/drop_analysis_whole_move.static_local.bir.dump "drop-state x: static" } }
#![feature(no_core)]
#![no_core]

struct Droppable {
    value: i32,
}

fn whole_move() {
    let x = Droppable { value: 1 };
    let y = x;
}

fn copy() {
    let x = 1;
    let y = x;
}

fn static_local() {
    let x = Droppable { value: 1 };
}
