import AppKit

enum DeviceFilledIcon {
    static func makeTemplateImage() -> NSImage {
        let size = NSSize(width: 18, height: 18)
        let image = NSImage(size: size)
        image.lockFocus()
        NSColor.white.setFill()

        // A tall, rounded silhouette matches the physical 2.06-inch display.
        let body = NSBezierPath(
            roundedRect: NSRect(x: 4, y: 1, width: 10, height: 16),
            xRadius: 3,
            yRadius: 3
        )
        body.fill()
        image.unlockFocus()
        image.isTemplate = true
        return image
    }
}
