import Foundation

struct FocusedTaskSelection: Equatable {
    enum Status: String { case confirmed, noTask, unsupportedHost, unavailable }
    var status: Status
    var threadId: String?
    var hostId: String?
    var observedAt: Date

    static func unavailable(at date: Date = Date()) -> Self {
        Self(status: .unavailable, observedAt: date)
    }

    /// Only the trusted main document origin is eligible, never arbitrary links
    /// in task text, an embedded browser, or a title that resembles a task ID.
    static func document(_ value: String, at date: Date = Date()) -> Self {
        guard value.count <= 4096, let url = URLComponents(string: value), url.scheme == "app",
              url.host == "-", url.user == nil, url.password == nil,
              url.port == nil, url.fragment == nil else { return .unavailable(at: date) }
        let parts = url.path.split(separator: "/", omittingEmptySubsequences: true)
        guard parts.count == 2, parts[0] == "local", parts[1] != "new" else {
            return Self(status: .noTask, observedAt: date)
        }
        let id = String(parts[1])
        // Restrict initial observation to actual persisted UUID task routes.
        // Temporary client routes must not be treated as established tasks.
        guard UUID(uuidString: id) != nil, url.percentEncodedPath == "/local/\(id)" else { return .unavailable(at: date) }
        let hosts = (url.queryItems ?? []).filter { $0.name == "hostId" }
        guard hosts.count <= 1 else { return .unavailable(at: date) }
        guard hosts.isEmpty || hosts[0].value?.isEmpty == false else { return .unavailable(at: date) }
        let host = hosts.first?.value ?? "local"
        guard host == "local" else {
            return Self(status: .unsupportedHost, hostId: host, observedAt: date)
        }
        return Self(status: .confirmed, threadId: id, hostId: host, observedAt: date)
    }

    func fresh(at now: Date = Date(), maximumAge: TimeInterval = 3) -> Self {
        guard now.timeIntervalSince(observedAt) >= 0,
              now.timeIntervalSince(observedAt) <= maximumAge else { return .unavailable(at: now) }
        return self
    }

    func voiceState(for targetId: String?, state: String) -> String {
        status == .confirmed && threadId != nil && threadId == targetId ? state : "unknown"
    }
}
