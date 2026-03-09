/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pictureinpicture

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import android.os.Build
import android.util.Log
import org.mozilla.fenix.R

internal object PipBubble {
    private const val TAG = "PipBubble"
    private const val CHANNEL_ID = "pip_bubble"
    private const val NOTIFICATION_ID = 0x50495000

    fun show(context: Context) {
        Log.d(TAG, "show: SDK=${Build.VERSION.SDK_INT} required=${Build.VERSION_CODES.R}")
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        val nm = context.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                context.getString(R.string.pip_bubble_channel_name),
                NotificationManager.IMPORTANCE_HIGH,
            ),
        )
        val intent = PendingIntent.getActivity(
            context,
            0,
            Intent(context, PipBubbleActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
        )
        val icon = Icon.createWithResource(context, R.drawable.ic_file_type_video)
        val bubbleMetadata = Notification.BubbleMetadata.Builder(intent, icon)
            .setDesiredHeight(1)
            .build()
        val notification = Notification.Builder(context, CHANNEL_ID)
            .setBubbleMetadata(bubbleMetadata)
            .setContentIntent(intent)
            .setSmallIcon(R.drawable.ic_file_type_video)
            .setContentTitle(context.getString(R.string.pip_bubble_notification_title))
            .setAutoCancel(true)
            .build()
        Log.d(TAG, "show: posting bubble notification")
        nm.notify(NOTIFICATION_ID, notification)
    }

    fun hide(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        context.getSystemService(NotificationManager::class.java).cancel(NOTIFICATION_ID)
    }
}
