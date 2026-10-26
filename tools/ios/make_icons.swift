// Render the existing Mac artwork into opaque iOS icon sizes without changing the source asset.
import Foundation
import CoreGraphics
import ImageIO

let source = URL(fileURLWithPath: CommandLine.arguments[1])
let catalog = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
let set = catalog.appendingPathComponent("AppIcon.appiconset", isDirectory: true)
try FileManager.default.createDirectory(at: set, withIntermediateDirectories: true)
guard let imageSource = CGImageSourceCreateWithURL(source as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(imageSource, 0, nil) else { fatalError("Cannot read Mac icon") }
let slots: [(String, Double, Int)] = [
    ("iphone",20,2),("iphone",20,3),("iphone",29,2),("iphone",29,3),
    ("iphone",40,2),("iphone",40,3),("iphone",60,2),("iphone",60,3),
    ("ipad",20,1),("ipad",20,2),("ipad",29,1),("ipad",29,2),
    ("ipad",40,1),("ipad",40,2),("ipad",76,1),("ipad",76,2),("ipad",83.5,2),
    ("ios-marketing",1024,1)
]
var entries: [[String: String]] = []
for (idiom, size, scale) in slots {
    let points = size.rounded() == size ? String(Int(size)) : String(size)
    let name = "icon-\(idiom)-\(points)-\(scale)x.png"
    let pixels = Int(size * Double(scale))
    guard let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8,
        bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
        else { fatalError("Cannot allocate icon bitmap") }
    context.setFillColor(CGColor(gray: 0, alpha: 1))
    context.fill(CGRect(x: 0, y: 0, width: pixels, height: pixels))
    context.interpolationQuality = .high
    context.draw(image, in: CGRect(x: 0, y: 0, width: pixels, height: pixels))
    guard let rendered = context.makeImage(), let destination = CGImageDestinationCreateWithURL(
        set.appendingPathComponent(name) as CFURL, "public.png" as CFString, 1, nil)
        else { fatalError("Cannot write icon") }
    CGImageDestinationAddImage(destination, rendered, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("Icon encoding failed") }
    entries.append(["idiom":idiom,"size":"\(points)x\(points)","scale":"\(scale)x","filename":name])
}
let contents: [String: Any] = ["images":entries,"info":["author":"xcode","version":1]]
try JSONSerialization.data(withJSONObject: contents, options: [.prettyPrinted,.sortedKeys])
    .write(to: set.appendingPathComponent("Contents.json"))
