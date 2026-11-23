// NetWire status for Kotlin / Android apps (Moquil, MP Voice, a companion app) on Fuel
// (fork github.com/JebetrexxMC/fuel). Reads wires.json from the netwire-status branch and returns the
// best wire of a group, for example the fastest crossplay path.
//   implementation("com.github.kittinunf.fuel:fuel:3.0.0-alpha04")   // or the fork through JitPack
//   val best = NetWireFuel.best("crossplay")   // suspend; null when nothing is up
package mp.netwire

import fuel.Fuel
import fuel.get
import org.json.JSONObject

data class Wire(val id: String, val group: String, val host: String, val port: Int, val name: String,
                val grade: String, val rtt: Double?, val loss: Double)

object NetWireFuel {
    const val URL = "https://raw.githubusercontent.com/JebetrexxMC/wagu/netwire-status/wires.json"

    suspend fun load(): Pair<Map<String, String>, List<Wire>> {
        val body = Fuel.get(URL).source.readUtf8()
        val o = JSONObject(body)
        val best = o.optJSONObject("best")?.let { b -> b.keys().asSequence().associateWith { b.getString(it) } } ?: emptyMap()
        val arr = o.getJSONArray("wires")
        val wires = (0 until arr.length()).map { i ->
            val w = arr.getJSONObject(i)
            Wire(w.getString("id"), w.getString("group"), w.getString("host"), w.getInt("port"), w.optString("name"),
                 w.optString("grade", "down"), if (w.isNull("rtt")) null else w.getDouble("rtt"), w.optDouble("loss", 100.0))
        }
        return best to wires
    }

    /** Best wire of [group] as picked by NetWire (lowest score), or the lowest-ping one that is up. */
    suspend fun best(group: String): Wire? {
        val (best, wires) = load()
        best[group]?.let { id -> wires.firstOrNull { it.id == id && it.grade != "down" }?.let { return it } }
        return wires.filter { it.group == group && it.grade != "down" && it.rtt != null }.minByOrNull { it.rtt!! }
    }
}
