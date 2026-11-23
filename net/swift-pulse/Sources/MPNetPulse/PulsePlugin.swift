// A Moya plugin that stores every request/response in Pulse's LoggerStore.
//
//   let provider = MoyaProvider<MPNetAPI>(plugins: [MPNetPulsePlugin()])
//   // later: PulseUI.ConsoleView() shows the NetWire calls, timings and bodies
import Foundation
import Moya
import MPNet
import Pulse

public final class MPNetPulsePlugin: PluginType {
    private let store: LoggerStore
    private let label: String

    public init(store: LoggerStore = .shared, label: String = "mpnet") {
        self.store = store
        self.label = label
    }

    public func didReceive(_ result: Result<Response, MoyaError>, target: TargetType) {
        switch result {
        case .success(let r):
            store.storeRequest(r.request ?? URLRequest(url: target.baseURL.appendingPathComponent(target.path)),
                               response: r.response, error: nil, data: r.data, label: label)
        case .failure(let e):
            let req = e.response?.request ?? URLRequest(url: target.baseURL.appendingPathComponent(target.path))
            store.storeRequest(req, response: e.response?.response, error: e, data: e.response?.data, label: label)
            store.storeMessage(label: label, level: .error, message: "\(target.path): \(e.localizedDescription)")
        }
    }
}
