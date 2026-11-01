// swift-tools-version: 6.0
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "SameBoy",
    products: [
        .library(name: "SameBoyCore", targets: ["SameBoyCore"]),
    ],
    targets: [
        .target(
            name: "SameBoyCore",
            path: "Core",
            publicHeadersPath: ".",
            cSettings: [
                .define("GB_INTERNAL"),
                .define("GB_VERSION", to: "\"1.0.1\"")
            ]
        ),
        .testTarget(
            name: "SameBoyCoreTests",
            dependencies: ["SameBoyCore"],
            path: "SameBoyCoreTests"
        )
    ]
)
