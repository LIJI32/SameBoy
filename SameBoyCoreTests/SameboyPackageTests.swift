import XCTest
import SameBoyCore

final class SameBoyCoreTests: XCTestCase {
    func test_GB_init() throws {
        // check if it compiles and does not crash
        
        var gb = GB_gameboy_t()
        GB_init(&gb, GB_MODEL_MGB)
        GB_free(&gb)
    }
}
