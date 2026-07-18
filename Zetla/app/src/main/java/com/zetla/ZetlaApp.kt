package com.zetla

import android.app.Application
import com.zetla.data.PdfExtractor
import com.zetla.data.ZetlaPython
import dagger.hilt.android.HiltAndroidApp

@HiltAndroidApp
class ZetlaApp : Application() {
    override fun onCreate() {
        super.onCreate()
        ZetlaPython.init(this)
        PdfExtractor.init(this)
    }
}
