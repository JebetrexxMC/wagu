import XCTest
import Moya
@testable import MPNet

final class MPNetTests: XCTestCase {
    func testSampleDecodes() throws {
        let s = try JSONDecoder().decode(NetWireStatus.self, from: MPNetAPI.wires.sampleData)
        XCTAssertEqual(s.best("lobby")?.host, "broker.emqx.io")
    }

    func testStubbedProvider() {
        let c = MPNetClient(provider: MoyaProvider<MPNetAPI>(stubClosure: MoyaProvider.immediatelyStub))
        let e = expectation(description: "status")
        c.status { r in if case .success(let s) = r { XCTAssertEqual(s.up, 1) } else { XCTFail("\(r)") }; e.fulfill() }
        wait(for: [e], timeout: 2)
    }
}
