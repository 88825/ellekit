
import Foundation

// target:replacement
public var hooks: [UnsafeMutableRawPointer: UnsafeMutableRawPointer] = [:]

public var slide: Int = _dyld_get_image_vmaddr_slide(0)

var exceptionHandler: ExceptionHandler?
