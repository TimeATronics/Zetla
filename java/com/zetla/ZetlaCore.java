package com.zetla;

public class ZetlaCore {
    static {
        System.loadLibrary("zetla_jni");
    }

    public static native String nativeVersion();
    public static native String nativeInit();
    public static native void nativeShutdown();
    public static native String nativeCreateSession(String model, String systemPrompt);
    public static native boolean nativeSendMessage(String sessionId, String message, ChatCallback callback);
    public static native boolean nativeSendMessageSse(String sessionId, String message, SseCallback callback);
    public static native String nativeDeleteSession(String sessionId);
    public static native String nativeGetSessionInfo(String sessionId);
    public static native String nativeGetHistory(String sessionId);
    public static native String nativeClearHistory(String sessionId);
    public static native String nativeSetSessionOptions(String sessionId, String optionsJson);
    public static native String nativeGetSessionOptions(String sessionId);
    public static native String nativeLoadSession(String sessionId);
    public static native String nativeListSessions();
    public static native String nativeDeleteFromStorage(String sessionId);
    public static native String nativeSessionExistsOnDisk(String sessionId);
    public static native String nativeCompactSession(String sessionId);
    public static native String nativeGetCompactionInfo(String sessionId);
    public static native String nativeAddTool(String sessionId, String name, String description, String parametersSchema);
    public static native boolean nativeSendMessageAgentic(String sessionId, String message, AgenticCallback callback);

    public static String version() {
        return nativeVersion();
    }

    public static boolean init() {
        String result = nativeInit();
        return result.contains("\"success\":true");
    }

    public static void shutdown() {
        nativeShutdown();
    }

    public static String createSession(String model, String systemPrompt) {
        return nativeCreateSession(model, systemPrompt);
    }

    public static boolean sendMessage(String sessionId, String message, ChatCallback callback) {
        return nativeSendMessage(sessionId, message, callback);
    }

    public static boolean sendMessageSse(String sessionId, String message, SseCallback callback) {
        return nativeSendMessageSse(sessionId, message, callback);
    }

    public static String deleteSession(String sessionId) {
        return nativeDeleteSession(sessionId);
    }

    public static String getSessionInfo(String sessionId) {
        return nativeGetSessionInfo(sessionId);
    }

    public static String getHistory(String sessionId) {
        return nativeGetHistory(sessionId);
    }

    public static String clearHistory(String sessionId) {
        return nativeClearHistory(sessionId);
    }

    public static String setSessionOptions(String sessionId, String optionsJson) {
        return nativeSetSessionOptions(sessionId, optionsJson);
    }

    public static String getSessionOptions(String sessionId) {
        return nativeGetSessionOptions(sessionId);
    }

    public static String loadSession(String sessionId) {
        return nativeLoadSession(sessionId);
    }

    public static String listSessions() {
        return nativeListSessions();
    }

    public static String deleteFromStorage(String sessionId) {
        return nativeDeleteFromStorage(sessionId);
    }

    public static String sessionExistsOnDisk(String sessionId) {
        return nativeSessionExistsOnDisk(sessionId);
    }

    public static String compactSession(String sessionId) {
        return nativeCompactSession(sessionId);
    }

    public static String getCompactionInfo(String sessionId) {
        return nativeGetCompactionInfo(sessionId);
    }

    public static String addTool(String sessionId, String name, String description, String parametersSchema) {
        return nativeAddTool(sessionId, name, description, parametersSchema);
    }

    public static boolean sendMessageAgentic(String sessionId, String message, AgenticCallback callback) {
        return nativeSendMessageAgentic(sessionId, message, callback);
    }

    public interface AgenticCallback {
        void onEvent(String eventJson);
    }
}
