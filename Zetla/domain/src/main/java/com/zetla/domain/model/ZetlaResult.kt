package com.zetla.domain.model

data class ZetlaResult<T>(
    val success: Boolean,
    val data: T? = null,
    val error: String? = null
) {
    companion object {
        fun <T> success(data: T) = ZetlaResult(true, data)
        fun <T> error(msg: String) = ZetlaResult<T>(false, error = msg)
    }
}
