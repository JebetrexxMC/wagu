// The same NetWire status on Fast Android Networking (fork github.com/JebetrexxMC/Fast-Android-Networking),
// for older Android apps that already use it (callback style, no coroutines).
//   implementation("com.amitshekhar.android:android-networking:1.0.4")
//   AndroidNetworking.initialize(applicationContext)
//   NetWireFan.best("crossplay") { wire -> ... }   // called on the main thread, wire = null when all are down
package mp.netwire

import com.androidnetworking.AndroidNetworking
import com.androidnetworking.common.Priority
import com.androidnetworking.error.ANError
import com.androidnetworking.interfaces.JSONObjectRequestListener
import org.json.JSONObject

object NetWireFan {
    fun best(group: String, done: (Wire?) -> Unit) {
        AndroidNetworking.get(NetWireFuel.URL)
            .setPriority(Priority.LOW)
            .doNotCacheResponse()
            .build()
            .getAsJSONObject(object : JSONObjectRequestListener {
                override fun onResponse(o: JSONObject) {
                    val id = o.optJSONObject("best")?.optString(group)
                    val arr = o.getJSONArray("wires")
                    var pick: Wire? = null
                    for (i in 0 until arr.length()) {
                        val w = arr.getJSONObject(i)
                        if (w.getString("group") != group || w.optString("grade") == "down" || w.isNull("rtt")) continue
                        val c = Wire(w.getString("id"), group, w.getString("host"), w.getInt("port"), w.optString("name"),
                                     w.optString("grade"), w.getDouble("rtt"), w.optDouble("loss", 100.0))
                        if (c.id == id) { pick = c; break }
                        if (pick == null || c.rtt!! < pick.rtt!!) pick = c
                    }
                    done(pick)
                }
                override fun onError(e: ANError) = done(null)
            })
    }
}
