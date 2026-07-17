package com.zetla;

public interface SseCallback {
    void onSseData(String jsonData);
    void onSseDone();
}
