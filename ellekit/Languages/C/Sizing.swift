
// This file is licensed under the BSD-3 Clause License
// Copyright 2022 © Charlotte Belanger

import Foundation

func checkBranch(_ opcode: UInt64) -> Bool {

    let opcode = reverse(UInt32(opcode))
            
    // 42
    if opcode >> 25 == b.base >> 25 {
        return true
    } else if opcode >> 25 == bl.base >> 25 {
        return false
    } else if opcode >> 25 == b.condBase >> 25 {
        return false
    }

    return false
}

func checkBranch(_ isn: [UInt8]) -> Bool {
    let isn: UInt64 = (UInt64(isn[3]) | UInt64(isn[2]) << 8 | UInt64(isn[1]) << 16 | UInt64(isn[0]) << 24)
    if isn == 0x7F2303D5 { // ignore pacibsp
        return false
    }
    return checkBranch(isn)
}

func findFunctionSize(_ target: UnsafeMutableRawPointer) -> Int? {

    let instructions: [UInt8] = target.withMemoryRebound(to: UInt8.self, capacity: 4 * 5, { ptr in
        Array(UnsafeMutableBufferPointer(start: ptr, count: 4 * 5))
    })
    let isns: [[UInt8]] = (0..<(instructions.count / 4)).map { offset in
        let base = offset*4
        return [
            instructions[base+0],
            instructions[base+1],
            instructions[base+2],
            instructions[base+3]
        ]
    }
    for (idx, isn) in isns.enumerated() {
        if checkBranch(isn) {
            return (idx + 1)
        }
    }
    return nil
}
