package com.zetla;

public interface ChatCallback {
    void onToken(String jsonChunk);
    void onFinished();
}
