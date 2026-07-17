package com.zetla.di

import android.content.Context
import android.content.SharedPreferences
import com.zetla.data.repository.ChatRepositoryImpl
import com.zetla.data.repository.ConfigRepositoryImpl
import com.zetla.data.repository.FileRepositoryImpl
import com.zetla.data.repository.SessionRepositoryImpl
import com.zetla.domain.repository.ChatRepository
import com.zetla.domain.repository.ConfigRepository
import com.zetla.domain.repository.FileRepository
import com.zetla.domain.repository.SessionRepository
import dagger.Binds
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
abstract class RepositoryModule {

    @Binds
    @Singleton
    abstract fun bindChatRepository(impl: ChatRepositoryImpl): ChatRepository

    @Binds
    @Singleton
    abstract fun bindSessionRepository(impl: SessionRepositoryImpl): SessionRepository

    @Binds
    @Singleton
    abstract fun bindFileRepository(impl: FileRepositoryImpl): FileRepository

    @Binds
    @Singleton
    abstract fun bindConfigRepository(impl: ConfigRepositoryImpl): ConfigRepository

    companion object {
        private const val PREFS_NAME = "ZetlaPrefs"
        private const val KEY_API_KEY = "api_key"
        private const val KEY_MODEL = "model"

        @Provides
        @Singleton
        fun provideSharedPreferences(@ApplicationContext context: Context): SharedPreferences {
            return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        }
    }
}
