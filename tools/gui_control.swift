// macOS native UI test driver, scoped to the test client's process id.
import AppKit
import CoreGraphics
import Foundation
import ApplicationServices

let args = CommandLine.arguments
guard args.count >= 3, let pid = Int32(args[2]) else { exit(2) }
let action = args[1]
if action == "permissions" {
    let data = try JSONSerialization.data(withJSONObject: ["accessibility": AXIsProcessTrusted(), "post_events": CGPreflightPostEventAccess(), "screen_capture": CGPreflightScreenCaptureAccess()])
    FileHandle.standardOutput.write(data)
    exit(0)
}
if action == "window" {
    let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
    let own = windows.filter { ($0[kCGWindowOwnerPID as String] as? Int32) == pid && ($0[kCGWindowLayer as String] as? Int) == 0 }
    guard let window = own.first else { exit(3) }
    let data = try JSONSerialization.data(withJSONObject: ["id": window[kCGWindowNumber as String]!, "bounds": window[kCGWindowBounds as String]!])
    FileHandle.standardOutput.write(data)
    exit(0)
}
guard let app = NSRunningApplication(processIdentifier: pid) else { exit(3) }
app.activate(options: [.activateAllWindows])
let accessibilityApp = AXUIElementCreateApplication(pid)
AXUIElementSetAttributeValue(accessibilityApp, kAXFrontmostAttribute as CFString, kCFBooleanTrue)
var windowValue: CFTypeRef?
if AXUIElementCopyAttributeValue(accessibilityApp, kAXWindowsAttribute as CFString, &windowValue) == .success,
   let ownWindows = windowValue as? [AXUIElement], let first = ownWindows.first {
    AXUIElementPerformAction(first, kAXRaiseAction as CFString)
}
for _ in 0..<20 {
    if NSWorkspace.shared.frontmostApplication?.processIdentifier == pid { break }
    RunLoop.current.run(until: Date().addingTimeInterval(0.05))
}
guard NSWorkspace.shared.frontmostApplication?.processIdentifier == pid else { fputs("test client could not become active\n", stderr); exit(5) }
if action == "focus" { exit(0) }
if action == "click" {
    guard args.count == 5, let x = Double(args[3]), let y = Double(args[4]) else { exit(2) }
    let point = CGPoint(x: x, y: y)
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(30000)
    for kind in [CGEventType.leftMouseDown, CGEventType.leftMouseUp] {
        guard let event = CGEvent(mouseEventSource: nil, mouseType: kind, mouseCursorPosition: point, mouseButton: .left) else { exit(4) }
        event.post(tap: .cghidEventTap)
        usleep(30000)
    }
} else if action == "type" {
    let text = String(data: FileHandle.standardInput.readDataToEndOfFile(), encoding: .utf8) ?? ""
    for character in text {
        let units = Array(String(character).utf16)
        for down in [true, false] {
            guard let event = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: down) else { exit(4) }
            units.withUnsafeBufferPointer { buffer in
                event.keyboardSetUnicodeString(stringLength: units.count, unicodeString: buffer.baseAddress!)
            }
            event.post(tap: .cghidEventTap)
        }
        usleep(12000)
    }
} else if action == "key" {
    let keys: [String: CGKeyCode] = ["left": 123, "right": 124, "down": 125, "up": 126, "1": 18, "2": 19, "3": 20, "4": 21, "enter": 36, "select_all": 0, "clear": 51]
    guard args.count == 4, let key = keys[args[3]] else { exit(2) }
    for _ in 0..<(args[3] == "clear" ? 64 : 1) {
      for down in [true, false] {
        guard let event = CGEvent(keyboardEventSource: nil, virtualKey: key, keyDown: down) else { exit(4) }
        if args[3] == "select_all" { event.flags = .maskCommand }
        event.post(tap: .cghidEventTap)
        usleep(args[3] == "clear" ? 1000 : 30000)
      }
    }
} else { exit(2) }
