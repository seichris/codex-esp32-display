import Foundation

/// Keeps the complete text of one recognition request when on-device Speech
/// resets `bestTranscription` after a pause between utterances.
///
/// Speech results can be either a cumulative hypothesis or a completed partial
/// utterance followed by a fresh hypothesis. The completed-partial marker is
/// supplied by `DictationRecorder` from `speechRecognitionMetadata`.
struct DictationTranscriptAccumulator {
    private(set) var completedText = ""
    private(set) var currentHypothesis = ""

    var text: String { Self.join(completedText, currentHypothesis) }

    @discardableResult
    mutating func update(_ rawText: String, completedPartial: Bool) -> String {
        let incoming = Self.trimmed(rawText)
        guard !incoming.isEmpty else { return text }

        if completedPartial {
            commitCompletedPartial(incoming)
        } else if let suffix = Self.suffix(of: incoming, after: completedText) {
            // Some recognizers continue returning the whole transcript after a
            // completed partial; keep only the new hypothesis in that case.
            currentHypothesis = suffix
        } else if currentHypothesis.isEmpty {
            currentHypothesis = incoming
        } else if Self.hasPrefix(incoming, currentHypothesis) {
            // Normal partial-result growth or a revision of the current phrase.
            currentHypothesis = incoming
        } else if Self.hasPrefix(currentHypothesis, incoming) {
            // A shorter stale hypothesis must not erase a more complete one.
        } else if Self.looksLikeRevision(currentHypothesis, incoming) {
            // Speech may revise the last words without preserving an exact
            // character prefix (for example, "turn rite" -> "turn right").
            currentHypothesis = incoming
        } else {
            // Fallback for platforms that omit the completed-partial marker:
            // treat a disjoint result as the next utterance.
            completedText = Self.mergeCompleted(completedText, currentHypothesis)
            currentHypothesis = incoming
        }

        return text
    }

    mutating func reset() {
        completedText = ""
        currentHypothesis = ""
    }

    private mutating func commitCompletedPartial(_ incoming: String) {
        if !currentHypothesis.isEmpty {
            if Self.hasPrefix(incoming, currentHypothesis) {
                // The new result is a fuller revision of the active phrase.
                currentHypothesis = ""
            } else if Self.hasPrefix(currentHypothesis, incoming) {
                // Keep the longer active hypothesis rather than erasing words.
                let active = currentHypothesis
                currentHypothesis = ""
                completedText = Self.mergeCompleted(completedText, active)
                return
            } else {
                // A reset can arrive without a clean marker on the preceding
                // callback. Preserve both the previous and incoming utterance.
                completedText = Self.mergeCompleted(completedText, currentHypothesis)
                currentHypothesis = ""
            }
        }
        completedText = Self.mergeCompleted(completedText, incoming)
    }

    private static func trimmed(_ value: String) -> String {
        value.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func join(_ first: String, _ second: String) -> String {
        guard !first.isEmpty else { return second }
        guard !second.isEmpty else { return first }
        return first + " " + second
    }

    private static func hasPrefix(_ value: String, _ prefix: String) -> Bool {
        guard !prefix.isEmpty else { return true }
        return value.lowercased().hasPrefix(prefix.lowercased())
    }

    private static func suffix(of value: String, after prefix: String) -> String? {
        guard !prefix.isEmpty, hasPrefix(value, prefix) else { return nil }
        let start = value.index(value.startIndex, offsetBy: min(prefix.count, value.count))
        return trimmed(String(value[start...]))
    }

    private static func mergeCompleted(_ existing: String, _ incoming: String) -> String {
        let existing = trimmed(existing)
        let incoming = trimmed(incoming)
        guard !existing.isEmpty else { return incoming }
        guard !incoming.isEmpty else { return existing }
        if hasPrefix(incoming, existing) { return incoming }
        if hasPrefix(existing, incoming) { return existing }
        return join(existing, incoming)
    }

    private static func looksLikeRevision(_ old: String, _ new: String) -> Bool {
        let oldWords = old.split(whereSeparator: { $0.isWhitespace })
        let newWords = new.split(whereSeparator: { $0.isWhitespace })
        guard !oldWords.isEmpty, !newWords.isEmpty else { return false }

        let shared = zip(oldWords, newWords).prefix { lhs, rhs in
            lhs.caseInsensitiveCompare(rhs) == .orderedSame
        }.count
        return shared >= max(1, min(oldWords.count, newWords.count) - 1)
    }
}
