package dev.ytmusic

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import dev.ytmusic.core.PlaylistRef
import dev.ytmusic.core.Section
import dev.ytmusic.core.Track
import dev.ytmusic.core.Yt

class MainActivity : Activity() {
    private enum class Tab(val label: String, val icon: String) {
        SEARCH("For you", "home"), QUEUE("Queue", "list"), LIKED("Liked", "heart"), PLAYLISTS("Playlists", "bookmark"), DOWNLOADS("Downloads", "download")
    }

    private sealed class Item {
        class T(val track: Track, val ctx: List<Track>? = null, val ctxIndex: Int = 0) : Item()
        object Hero : Item()
        class Header(val title: String, val subtitle: String, val tracks: List<Track>, val buttons: Boolean = true) : Item()
        class Strip(val tracks: List<Track>) : Item()
        class Remote(val ref: PlaylistRef) : Item()
        class Saved(val p: SavedPlaylist) : Item()
        object Footer : Item()
    }

    private var tab = Tab.SEARCH
    private var savedId: String? = null  // saved playlist opened in the Playlists tab

    private lateinit var searchEdit: EditText
    private lateinit var clearBtn: IconBtn
    private lateinit var headScroll: HorizontalScrollView
    private lateinit var headerRow: LinearLayout
    private lateinit var list: RecyclerView
    private lateinit var emptyText: TextView
    private lateinit var chips: LinearLayout
    private lateinit var chipFlow: FlowLayout
    private lateinit var suggestBox: LinearLayout
    private lateinit var updateBar: TextView
    private lateinit var mini: MiniPlayerView
    private lateinit var nav: BottomNavView
    private lateinit var now: NowPlayingView
    private val adapter = Adapter()
    private var items: List<Item> = emptyList()
    private var started = false
    private var reordering = false
    private var suggestGen = 0
    private var selecting = false          // multi-select mode (Queue, Liked, Downloads, Playlists)
    private val selected = HashSet<String>() // video ids, or playlist ids on the playlists list
    private var suggestOn = false        // suggestions only show once you tap or type in the search box
    private var programmatic = false     // text set by the app itself (suggestion picked, chip tapped) is not "typing"
    private lateinit var rootView: FrameLayout

    // ---------------------------------------------------------------- UI construction
    override fun onCreate(savedInstanceState: Bundle?) {
        setTheme(when (C.name) { "light" -> R.style.AppTheme_Light; "amoled" -> R.style.AppTheme_Amoled; else -> R.style.AppTheme })
        super.onCreate(savedInstanceState)
        window.statusBarColor = C.surface
        window.navigationBarColor = C.surfaceVariant
        if (android.os.Build.VERSION.SDK_INT >= 23 && C.isLight) {
            var f = window.decorView.systemUiVisibility or View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
            if (android.os.Build.VERSION.SDK_INT >= 26) f = f or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
            window.decorView.systemUiVisibility = f
        }
        val root = FrameLayout(this)
        rootView = root
        root.setBackgroundColor(C.surface)
        root.isFocusable = true; root.isFocusableInTouchMode = true   // parks keyboard focus away from the search box
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(col, FrameLayout.LayoutParams(-1, -1))

        // search bar: Material-style filled field with the app menu at its end
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            background = GradientDrawable().apply { setColor(C.surfaceVariant); cornerRadius = Ui.dp(28) }
            setPadding(Ui.dpi(6), 0, Ui.dpi(2), 0)
        }
        val lead = IconBtn(this).apply { icon = "search"; tint = C.onSurfaceVariant; setOnClickListener { runSearch() } }
        searchEdit = EditText(this).apply {
            hint = if (Ui.compact) "Search songs or paste a link" else "Search songs, playlists or paste a link"
            setHintTextColor(C.onSurfaceVariant)
            setTextColor(C.onSurface)
            textSize = 16f
            background = null
            setSingleLine()
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = EditorInfo.IME_ACTION_SEARCH
            setOnEditorActionListener { _, id, ev ->
                val enter = ev != null && ev.keyCode == android.view.KeyEvent.KEYCODE_ENTER
                if (id == EditorInfo.IME_ACTION_SEARCH || (enter && ev?.action == android.view.KeyEvent.ACTION_DOWN)) { runSearch(); true } else enter
            }
            addTextChangedListener(object : android.text.TextWatcher {
                override fun afterTextChanged(s: android.text.Editable?) {
                    clearBtn.visibility = if (s.isNullOrEmpty()) View.GONE else View.VISIBLE
                    if (!programmatic && searchEdit.isFocused) { suggestOn = true; fetchSuggestions(s?.toString() ?: "") }
                }
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
            })
        }
        searchEdit.setOnFocusChangeListener { _, focused -> if (!focused) suggestBox.visibility = View.GONE }
        searchEdit.setOnTouchListener { _, e ->
            if (e.action == android.view.MotionEvent.ACTION_UP) { suggestOn = true; fetchSuggestions(searchEdit.text.toString()) }
            false
        }
        clearBtn = IconBtn(this).apply { icon = "x"; tint = C.onSurfaceVariant; visibility = View.GONE; setOnClickListener { setSearchText(""); Library.clear(); suggestOn = true; showKeyboard(); fetchSuggestions("") } }
        val overflow = IconBtn(this).apply { icon = "more"; tint = C.onSurfaceVariant; setOnClickListener { showAppMenu(it) } }
        bar.addView(lead, LinearLayout.LayoutParams(Ui.dpi(44), Ui.dpi(44)))
        bar.addView(searchEdit, LinearLayout.LayoutParams(0, -2, 1f))
        bar.addView(clearBtn, LinearLayout.LayoutParams(Ui.dpi(44), Ui.dpi(44)))
        bar.addView(overflow, LinearLayout.LayoutParams(Ui.dpi(44), Ui.dpi(44)))
        col.addView(bar, LinearLayout.LayoutParams(-1, Ui.dpi(52)).apply { setMargins(Ui.dpi(12), Ui.dpi(10), Ui.dpi(12), Ui.dpi(6)) })

        // context chips
        headScroll = HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; overScrollMode = View.OVER_SCROLL_NEVER }
        headerRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(Ui.dpi(12), 0, Ui.dpi(12), 0) }
        headScroll.addView(headerRow, FrameLayout.LayoutParams(-2, -1))
        col.addView(headScroll, LinearLayout.LayoutParams(-1, Ui.dpi(46)))

        // list area (+ empty text, history chips, suggestions)
        val area = FrameLayout(this)
        list = RecyclerView(this).apply {
            layoutManager = LinearLayoutManager(this@MainActivity)
            adapter = this@MainActivity.adapter
            setHasFixedSize(true)
            itemAnimator = null
            setItemViewCacheSize(6)
            overScrollMode = View.OVER_SCROLL_NEVER
            clipToPadding = false
            setPadding(0, 0, 0, Ui.dpi(8))
        }
        area.addView(list, FrameLayout.LayoutParams(-1, -1))
        touchHelper.attachToRecyclerView(list)
        emptyText = TextView(this).apply { setTextColor(C.onSurfaceVariant); textSize = 15f; gravity = Gravity.CENTER; setPadding(Ui.dpi(32), 0, Ui.dpi(32), 0) }
        area.addView(emptyText, FrameLayout.LayoutParams(-1, -1))
        chips = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(Ui.dpi(16), Ui.dpi(8), Ui.dpi(16), 0) }
        val chipHead = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        chipHead.addView(TextView(this).apply { text = "Recent searches"; setTextColor(C.onSurfaceVariant); textSize = 13f }, LinearLayout.LayoutParams(0, -2, 1f))
        chipHead.addView(TextView(this).apply { text = "Clear"; setTextColor(C.primary); textSize = 13f; setPadding(Ui.dpi(8), Ui.dpi(8), 0, Ui.dpi(8)); setOnClickListener { Store.clearHistory() } })
        chips.addView(chipHead)
        chipFlow = FlowLayout(this)
        chips.addView(chipFlow, LinearLayout.LayoutParams(-1, -2).apply { topMargin = Ui.dpi(6) })
        area.addView(chips, FrameLayout.LayoutParams(-1, -2))
        suggestBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(C.surface); visibility = View.GONE; isClickable = true; setOnClickListener { closeSuggestions(); hideKeyboard() } }
        area.addView(suggestBox, FrameLayout.LayoutParams(-1, -1))
        col.addView(area, LinearLayout.LayoutParams(-1, 0, 1f))

        // "update available" banner
        updateBar = TextView(this).apply {
            setTextColor(C.onSecondaryContainer); textSize = 14f; typeface = Ui.medium; gravity = Gravity.CENTER_VERTICAL
            setPadding(Ui.dpi(16), 0, Ui.dpi(16), 0)
            background = GradientDrawable().apply { setColor(C.secondaryContainer); cornerRadius = Ui.dp(16) }
            visibility = View.GONE
            setOnClickListener { Updater.downloadAndInstall { toast(it) } }
        }
        col.addView(updateBar, LinearLayout.LayoutParams(-1, Ui.dpi(44)).apply { setMargins(Ui.dpi(12), Ui.dpi(2), Ui.dpi(12), Ui.dpi(2)) })

        mini = MiniPlayerView(this).apply { onOpen = { openNowPlaying() } }
        col.addView(mini, LinearLayout.LayoutParams(-1, Ui.dpi(72)))

        nav = BottomNavView(this, Tab.values().map { it.icon to it.label }).apply { onSelect = { selectTab(Tab.values()[it]) } }
        col.addView(nav, LinearLayout.LayoutParams(-1, Ui.dpi(68)))

        // full-screen player overlay
        now = NowPlayingView(this).apply {
            visibility = View.GONE
            onClose = { closeNowPlaying() }
            onSleep = { showSleepDialog() }
            onQueue = { closeNowPlaying(); selectTab(Tab.QUEUE) }
            onLike = { Playback.current?.let { Store.toggleLike(it) } }
            onDownload = { Playback.current?.let { toggleDownload(it) } }
        }
        root.addView(now, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)
        val pref = Settings.startTab
        val saved = if (pref == "LAST") getSharedPreferences("ui", MODE_PRIVATE).getString("tab", Tab.SEARCH.name) else pref
        selectTab(try { Tab.valueOf(saved ?: "SEARCH") } catch (_: Throwable) { Tab.SEARCH })
    }

    // ---------------------------------------------------------------- lifecycle / refresh
    private val dataListener = { dirtyData = true; scheduleRefresh() }
    private val drawListener = { scheduleRefresh() }
    private val playbackListener = { if (tab == Tab.QUEUE) dirtyData = true; scheduleRefresh() }
    private val updateListener = { renderUpdateBar() }
    private val forYouListener = { if (feedMode()) dirtyData = true; scheduleRefresh() }
    private val downloadsListener = { if (tab == Tab.DOWNLOADS) dirtyData = true; scheduleRefresh() }
    private var dirtyData = false
    private var refreshQueued = false
    private val refreshRun = Runnable {
        refreshQueued = false
        if (dirtyData && !reordering) { dirtyData = false; rebuild() }
        // rows keep cached display lists: invalidating the RecyclerView alone does not redraw them
        for (i in 0 until list.childCount) list.getChildAt(i).invalidate()
        mini.invalidate(); if (now.visibility == View.VISIBLE) now.invalidate()
    }
    private fun scheduleRefresh() { if (!refreshQueued) { refreshQueued = true; App.main.postDelayed(refreshRun, 40) } }

    private val tick: Runnable = object : Runnable {
        override fun run() {
            if (!started) return
            mini.invalidate()
            if (now.visibility == View.VISIBLE) now.invalidate()
            App.main.postDelayed(this, 500)
        }
    }

    override fun onStart() {
        super.onStart()
        started = true
        Library.listeners.add(dataListener); Store.listeners.add(dataListener)
        Playback.listeners.add(playbackListener); Downloads.listeners.add(downloadsListener); Thumbs.listeners.add(drawListener)
        Updater.listeners.add(updateListener); ForYou.listeners.add(forYouListener)
        ForYou.refreshIfStale()
        rebuild()
        renderUpdateBar()
        Updater.autoCheck()
        App.main.post(tick)
    }

    override fun onStop() {
        started = false
        closeSuggestions()
        hideKeyboard()
        Library.listeners.remove(dataListener); Store.listeners.remove(dataListener)
        Playback.listeners.remove(playbackListener); Downloads.listeners.remove(downloadsListener); Thumbs.listeners.remove(drawListener)
        Updater.listeners.remove(updateListener); ForYou.listeners.remove(forYouListener)
        App.main.removeCallbacks(tick)
        Playback.savePosition()
        super.onStop()
    }

    override fun onBackPressed() {
        when {
            selecting -> stopSelect()
            now.visibility == View.VISIBLE -> closeNowPlaying()
            suggestBox.visibility == View.VISIBLE -> { closeSuggestions(); hideKeyboard() }
            tab == Tab.SEARCH && Library.detail != null -> Library.closeDetail()
            tab == Tab.SEARCH && Library.query.isNotEmpty() -> { Library.clear(); setSearchText("") }
            tab == Tab.PLAYLISTS && savedId != null -> { savedId = null; rebuild() }
            else -> moveTaskToBack(true) // keep playing
        }
    }

    private fun renderUpdateBar() {
        val u = Updater.available
        if (u == null) { updateBar.visibility = View.GONE; return }
        updateBar.visibility = View.VISIBLE
        updateBar.text = if (Updater.progress >= 0) "Downloading update… ${Updater.progress}%"
                         else "Update available: v${u.name}  —  tap to install"
    }

    // ---------------------------------------------------------------- navigation
    private fun selectTab(t: Tab) {
        tab = t
        selecting = false; selected.clear()
        if (t != Tab.PLAYLISTS) savedId = null
        nav.selected = t.ordinal
        getSharedPreferences("ui", MODE_PRIVATE).edit().putString("tab", t.name).apply()
        closeSuggestions()
        rebuild()
        list.scrollToPosition(0)
    }

    private fun openNowPlaying() {
        now.visibility = View.VISIBLE
        if (Settings.keepScreenOn) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        now.translationY = now.height.toFloat().coerceAtLeast(Ui.dp(300))
        now.animate().translationY(0f).setDuration(200).start()
        hideKeyboard()
    }

    private fun closeNowPlaying() {
        window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        now.animate().translationY(now.height.toFloat()).setDuration(170).withEndAction { now.visibility = View.GONE }.start()
    }

    private fun runSearch() {
        val q = searchEdit.text.toString().trim()
        if (q.isEmpty()) return
        closeSuggestions()
        hideKeyboard()
        if (tab != Tab.SEARCH) selectTab(Tab.SEARCH)
        Library.search(q)
    }

    private fun setSearchText(t: String) {
        programmatic = true
        searchEdit.setText(t)
        searchEdit.setSelection(t.length)
        programmatic = false
    }

    private fun closeSuggestions() {
        suggestOn = false
        suggestGen++
        suggestBox.visibility = View.GONE
    }

    private fun showKeyboard() { searchEdit.requestFocus(); (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).showSoftInput(searchEdit, 0) }
    private fun hideKeyboard() {
        searchEdit.clearFocus()
        rootView.requestFocus()   // otherwise Android hands focus straight back to the search box
        (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(searchEdit.windowToken, 0)
    }

    // ---------------------------------------------------------------- search suggestions
    private fun fetchSuggestions(text: String) {
        val my = ++suggestGen
        if (!suggestOn) { suggestBox.visibility = View.GONE; return }
        if (text.isBlank()) {            // focused and empty: offer recent searches
            suggestBox.removeAllViews()
            if (searchEdit.hasFocus() && Store.history.isNotEmpty()) {
                for (q in Store.history.take(8)) suggestBox.addView(SuggestionView(this).apply { this.text = q; onPick = { setSearchText(q); runSearch() } }, LinearLayout.LayoutParams(-1, Ui.dpi(52)))
                suggestBox.visibility = View.VISIBLE
            } else suggestBox.visibility = View.GONE
            return
        }
        if (!searchEdit.hasFocus() || text.startsWith("http")) { suggestBox.visibility = View.GONE; return }
        App.main.postDelayed({
            if (my != suggestGen) return@postDelayed
            App.io.execute {
                val s = try { Yt.suggestions(text) } catch (_: Throwable) { emptyList() }
                App.main.post {
                    if (my != suggestGen || !searchEdit.hasFocus()) return@post
                    suggestBox.removeAllViews()
                    for (q in s) suggestBox.addView(SuggestionView(this).apply { this.text = q; onPick = { setSearchText(q); runSearch() } },
                        LinearLayout.LayoutParams(-1, Ui.dpi(52)))
                    suggestBox.visibility = if (s.isEmpty()) View.GONE else View.VISIBLE
                }
            }
        }, 220)
    }

    // ---------------------------------------------------------------- content model
    private fun savedPlaylist() = savedId?.let { Store.playlist(it) }

    /** The "For you" page is what the first tab shows until you search. */
    private fun feedMode() = tab == Tab.SEARCH && Library.detail == null && Library.query.isEmpty()

    private fun currentTracks(): List<Track> = when (tab) {
        Tab.SEARCH -> Library.detail?.tracks ?: if (Library.mode == Library.Mode.SONGS) Library.songs else emptyList()
        Tab.QUEUE -> Playback.queue()
        Tab.LIKED -> Store.liked
        Tab.PLAYLISTS -> savedPlaylist()?.tracks ?: emptyList()
        Tab.DOWNLOADS -> Downloads.tracks()
    }

    // ---------------------------------------------------------------- select & delete
    private fun canSelect() = tab == Tab.QUEUE || tab == Tab.LIKED || tab == Tab.DOWNLOADS || tab == Tab.PLAYLISTS
    private fun keyOf(it: Item): String? = when (it) { is Item.T -> it.track.vid; is Item.Saved -> it.p.id; else -> null }

    private fun startSelect(key: String?) {
        if (!canSelect()) return
        selecting = true; selected.clear(); key?.let { selected += it }
        rebuild()
    }

    private fun stopSelect() { selecting = false; selected.clear(); rebuild() }

    private fun toggleSel(key: String) {
        if (!selected.add(key)) selected.remove(key)
        renderHeader()
        adapter.notifyDataSetChanged()
    }

    private fun selectAll() {
        selected.clear()
        items.forEach { keyOf(it)?.let(selected::add) }
        renderHeader()
        adapter.notifyDataSetChanged()
    }

    /** Two separate confirmations, the second one stating plainly that it can't be undone. */
    private fun confirmTwice(first: String, second: String, onYes: () -> Unit) {
        AlertDialog.Builder(this).setMessage(first).setNegativeButton("Cancel", null)
            .setPositiveButton("Continue") { _, _ ->
                AlertDialog.Builder(this).setTitle("Are you sure?").setMessage(second).setNegativeButton("Cancel", null)
                    .setPositiveButton("Delete everything") { _, _ -> onYes() }.show()
            }.show()
    }

    private fun deleteSelected() {
        if (selected.isEmpty()) { toast("Nothing selected"); return }
        val n = selected.size
        val what = when (tab) {
            Tab.DOWNLOADS -> if (n == 1) "download" else "downloads"
            Tab.LIKED -> if (n == 1) "liked song" else "liked songs"
            Tab.QUEUE -> if (n == 1) "song from the queue" else "songs from the queue"
            else -> if (savedId != null) (if (n == 1) "song from this playlist" else "songs from this playlist") else (if (n == 1) "playlist" else "playlists")
        }
        confirm(if (tab == Tab.QUEUE || tab == Tab.LIKED || (tab == Tab.PLAYLISTS && savedId != null)) "Remove $n $what?" else "Delete $n $what?") {
            when (tab) {
                Tab.DOWNLOADS -> Downloads.deleteMany(selected.toList())
                Tab.LIKED -> Store.unlike(selected.toSet())
                Tab.QUEUE -> items.mapIndexedNotNull { i, it -> if (it is Item.T && it.track.vid in selected) i else null }.sortedDescending().forEach { Playback.removeAt(it) }
                else -> {
                    val id = savedId
                    if (id != null) Store.removeFromPlaylist(id, items.mapIndexedNotNull { i, it -> if (it is Item.T && it.track.vid in selected) i else null })
                    else Store.deletePlaylists(selected.toSet())
                }
            }
            toast("Done")
            stopSelect()
        }
    }

    private fun deleteAll() {
        val n = items.count { keyOf(it) != null }
        when (tab) {
            Tab.DOWNLOADS -> confirmTwice("Delete all $n downloads (${Ui.fmtBytes(Downloads.totalBytes())})?",
                "This permanently deletes every downloaded file from this device. It can't be undone.") {
                val freed = Downloads.deleteAll(); toast("Deleted all downloads (${Ui.fmtBytes(freed)} freed)")
            }
            Tab.LIKED -> confirmTwice("Remove all $n liked songs?", "Your whole Liked list will be emptied. It can't be undone.") { Store.clearLikes(); toast("Liked songs cleared") }
            Tab.QUEUE -> confirm("Clear the whole queue ($n songs)?") { Playback.clearQueue() }
            Tab.PLAYLISTS -> {
                val id = savedId
                if (id != null) confirmTwice("Remove all $n songs from \u201c${savedPlaylist()?.name ?: ""}\u201d?", "The playlist itself stays, but it will be empty. It can't be undone.") { Store.removeAllFromPlaylist(id); toast("Playlist emptied") }
                else confirmTwice("Delete all $n playlists?", "Every playlist you saved or created will be deleted. It can't be undone.") { Store.clearAllPlaylists(); toast("All playlists deleted") }
            }
            else -> {}
        }
    }

    private fun selectionChips(hasItems: Boolean) {
        if (!hasItems || !canSelect()) return
        pill("Select", "check") { startSelect(null) }
        pill("Delete all", "trash") { deleteAll() }
    }

    private fun computeItems(): List<Item> {
        val out = ArrayList<Item>()
        when (tab) {
            Tab.SEARCH -> if (feedMode()) {
                out += Item.Hero
                if (Store.recent.isNotEmpty()) {
                    val r = Store.recent.take(12)
                    out += Item.Header("Jump back in", "Pick up where you left off", r)
                    out += Item.Strip(r)
                }
                for (sec in ForYou.sections) {
                    out += Item.Header(sec.title, sec.subtitle, sec.tracks)
                    sec.tracks.take(8).forEachIndexed { i, t -> out += Item.T(t, sec.tracks, i) }
                }
            } else {
                val d = Library.detail
                if (d != null) {
                    d.tracks.forEach { out += Item.T(it) }
                    if (d.more != null) out += Item.Footer
                } else if (Library.mode == Library.Mode.SONGS) {
                    Library.songs.forEach { out += Item.T(it) }
                    if (Library.canLoadMore || (Library.searching && Library.songs.isNotEmpty())) out += Item.Footer
                } else {
                    Library.lists.forEach { out += Item.Remote(it) }
                    if (Library.canLoadMore || (Library.searching && Library.lists.isNotEmpty())) out += Item.Footer
                }
            }
            Tab.PLAYLISTS -> {
                val p = savedPlaylist()
                if (p != null) p.tracks.forEach { out += Item.T(it) } else Store.playlists.forEach { out += Item.Saved(it) }
            }
            else -> currentTracks().forEach { out += Item.T(it) }
        }
        return out
    }

    private fun rebuild() {
        if (tab == Tab.PLAYLISTS && savedId != null && savedPlaylist() == null) savedId = null
        items = computeItems()
        if (selecting) {                      // drop selections that no longer exist; leave select mode when nothing is left
            val keys = items.mapNotNull { keyOf(it) }.toSet()
            selected.retainAll(keys)
            if (keys.isEmpty()) { selecting = false; selected.clear() }
        }
        adapter.notifyDataSetChanged()
        renderHeader()
        nav.badges = intArrayOf(0, 0, 0, 0, if (Downloads.busy()) 1 else 0)
        nav.invalidate()

        val d = Library.detail
        val feed = feedMode()
        headScroll.visibility = if (feed) View.GONE else View.VISIBLE
        val msg: String = when {
            feed -> if (ForYou.sections.isEmpty() && !ForYou.building && Store.recent.isEmpty()) "" else ""
            items.isNotEmpty() -> ""
            tab == Tab.SEARCH && d != null -> if (d.loading) "Loading playlist…" else d.status.ifEmpty { "Empty playlist" }
            tab == Tab.SEARCH -> if (Library.searching) "Searching…" else if (Library.query.isEmpty() && Store.history.isEmpty()) "Search for songs or playlists" else Library.status
            tab == Tab.QUEUE -> "Queue is empty"
            tab == Tab.LIKED -> "No liked songs yet.\nTap the heart on a song."
            tab == Tab.PLAYLISTS -> if (savedId != null) "This playlist is empty" else "No playlists yet.\nCreate one, or save one from a playlist search."
            else -> "Nothing downloaded yet.\nTap the download icon on a song."
        }
        emptyText.text = msg
        emptyText.visibility = if (msg.isEmpty()) View.GONE else View.VISIBLE
        val showChips = false   // recent searches now appear under the search box when you tap it
        chips.visibility = if (showChips) View.VISIBLE else View.GONE
        if (showChips) {
            emptyText.visibility = View.GONE
            chipFlow.removeAllViews()
            for (q in Store.history) chipFlow.addView(PillView(this).apply { label = q; setOnClickListener { setSearchText(q); runSearch() } })
        }
    }

    // ---------------------------------------------------------------- header (context actions)
    private fun pill(label: String, icon: String = "", selected: Boolean = false, onClick: () -> Unit) {
        headerRow.addView(PillView(this).apply { this.label = label; this.icon = icon; selectedPill = selected; setOnClickListener { onClick() } },
            LinearLayout.LayoutParams(-2, -2).apply { rightMargin = Ui.dpi(8) })
    }

    private fun headerText(text: String, bold: Boolean = false, maxDp: Int = 200) {
        headerRow.addView(TextView(this).apply {
            this.text = text; setTextColor(if (bold) C.onSurface else C.onSurfaceVariant); textSize = 14f
            if (bold) typeface = Ui.medium
            maxWidth = Ui.dpi(maxDp); setSingleLine(); ellipsize = android.text.TextUtils.TruncateAt.END
            setPadding(Ui.dpi(4), 0, Ui.dpi(12), 0)
        })
    }

    private fun backPill(onBack: () -> Unit) {
        headerRow.addView(IconBtn(this).apply { icon = "back"; setOnClickListener { onBack() } }, LinearLayout.LayoutParams(Ui.dpi(40), Ui.dpi(40)))
    }

    private fun renderHeader() {
        headerRow.removeAllViews()
        val tracks = currentTracks()
        val hasTracks = tracks.isNotEmpty()
        if (selecting) {
            pill("Cancel", "x") { stopSelect() }
            headerText("${selected.size} selected", bold = true, maxDp = 130)
            pill("Select all", "check") { selectAll() }
            pill("Delete", "trash") { deleteSelected() }
            return
        }
        fun listActions() {
            if (!hasTracks) return
            pill("Play all", "play") { Playback.play(tracks, 0) }
            pill("Shuffle", "shuffle") { Playback.play(tracks, 0, shuffle = true) }
            selectionChips(true)
            pill("Queue all", "plus") { Playback.enqueue(tracks); toast("Added ${tracks.size} to queue") }
            pill("Download all", "download") { Downloads.enqueue(this, tracks); toast("Downloading ${tracks.size} songs") }
        }
        when (tab) {
            Tab.SEARCH -> {
                val d = Library.detail
                if (d != null) {
                    backPill { Library.closeDetail() }
                    headerText(d.title, bold = true)
                    listActions()
                    if (hasTracks) pill("Save playlist", "bookmark") { saveRemote(d.title, d.url) }
                } else {
                    pill("Songs", selected = Library.mode == Library.Mode.SONGS) { Library.setMode(Library.Mode.SONGS) }
                    pill("Playlists", selected = Library.mode == Library.Mode.PLAYLISTS) { Library.setMode(Library.Mode.PLAYLISTS) }
                    if (Library.mode == Library.Mode.SONGS) listActions()
                }
            }
            Tab.QUEUE -> {
                headerText(if (hasTracks) "${tracks.size} ${if (tracks.size == 1) "song" else "songs"}" else "", maxDp = 120)
                selectionChips(hasTracks)
                if (hasTracks) {
                    pill("Save as playlist", "bookmark") { promptName("Save queue as playlist", "My queue") { n -> val p = Store.createPlaylist(n, tracks); toast("Saved “${p.name}”") } }
                    pill("Download all", "download") { Downloads.enqueue(this, tracks); toast("Downloading ${tracks.size} songs") }
                }
            }
            Tab.LIKED -> {
                listActions()
                if (hasTracks) pill("Save as playlist", "bookmark") { promptName("Save liked songs as playlist", "Liked songs") { n -> Store.createPlaylist(n, tracks); toast("Saved") } }
            }
            Tab.PLAYLISTS -> {
                val p = savedPlaylist()
                if (p == null) {
                    pill("New playlist", "plus") { promptName("New playlist", "") { n -> Store.createPlaylist(n); } }
                    selectionChips(Store.playlists.isNotEmpty())
                } else {
                    backPill { savedId = null; rebuild() }
                    headerText(p.name, bold = true)
                    listActions()
                    pill("Rename") { promptName("Rename playlist", p.name) { n -> Store.renamePlaylist(p.id, n) } }
                    pill("Delete", "trash") { confirm("Delete “${p.name}”?") { Store.deletePlaylist(p.id) } }
                }
            }
            Tab.DOWNLOADS -> {
                if (hasTracks) {
                    pill("Play all", "play") { Playback.play(tracks, 0) }
                    pill("Shuffle", "shuffle") { Playback.play(tracks, 0, shuffle = true) }
                    selectionChips(true)
                    headerText("${tracks.size} ${if (tracks.size == 1) "song" else "songs"} · ${Ui.fmtBytes(Downloads.totalBytes())}", maxDp = 240)
                }
                if (Downloads.busy()) pill("Cancel downloads", "x") { Downloads.cancelAll() }
            }
        }
    }

    private fun saveRemote(name: String, url: String) {
        Library.saveRemote(name, url) { n -> toast("Saved “$name” ($n songs) to Playlists") }
    }

    // ---------------------------------------------------------------- list adapter
    private inner class Adapter : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
        override fun getItemCount() = items.size
        override fun getItemViewType(position: Int) = when (items[position]) {
            is Item.Footer -> 1
            Item.Hero -> 2
            is Item.Header -> 3
            is Item.Strip -> 4
            else -> 0
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            return if (viewType == 2) {
                val v = HeroView(parent.context); v.layoutParams = RecyclerView.LayoutParams(-1, Ui.dpi(78))
                object : RecyclerView.ViewHolder(v) {}
            } else if (viewType == 3) {
                val v = SectionHeaderView(parent.context); v.layoutParams = RecyclerView.LayoutParams(-1, Ui.dpi(64))
                object : RecyclerView.ViewHolder(v) {}
            } else if (viewType == 4) {
                val rv = RecyclerView(parent.context).apply {
                    layoutManager = LinearLayoutManager(parent.context, LinearLayoutManager.HORIZONTAL, false)
                    overScrollMode = View.OVER_SCROLL_NEVER; itemAnimator = null; setHasFixedSize(true)
                    setPadding(Ui.dpi(10), 0, Ui.dpi(10), 0); clipToPadding = false
                    layoutParams = RecyclerView.LayoutParams(-1, Ui.dpi(192))
                }
                object : RecyclerView.ViewHolder(rv) {}
            } else if (viewType == 1) {
                val wrap = FrameLayout(parent.context)
                wrap.layoutParams = RecyclerView.LayoutParams(-1, Ui.dpi(64))
                object : RecyclerView.ViewHolder(wrap) {}
            } else {
                val v = RowView(parent.context)
                v.layoutParams = RecyclerView.LayoutParams(-1, Ui.dpi(72))
                object : RecyclerView.ViewHolder(v) {}
            }
        }

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            when (val it = items[position]) {
                is Item.T -> bindTrack(holder.itemView as RowView, it.track, position, it)
                Item.Hero -> (holder.itemView as HeroView).let { h -> h.building = ForYou.building; h.onRefresh = { ForYou.refresh(); toast("Updating your picks\u2026") } }
                is Item.Header -> (holder.itemView as SectionHeaderView).let { h ->
                    h.title = it.title; h.subtitle = it.subtitle; h.showButtons = it.buttons && it.tracks.isNotEmpty()
                    h.onPlay = { Playback.play(it.tracks, 0) }
                    h.onShuffle = { Playback.play(it.tracks, 0, shuffle = true) }
                }
                is Item.Strip -> bindStrip(holder.itemView as RecyclerView, it.tracks)
                is Item.Remote -> bindRemote(holder.itemView as RowView, it.ref)
                is Item.Saved -> bindSaved(holder.itemView as RowView, it.p)
                Item.Footer -> bindFooter(holder.itemView as FrameLayout)
            }
        }
    }

    private fun bindStrip(rv: RecyclerView, tracks: List<Track>) {
        rv.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun getItemCount() = tracks.size
            override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) = object : RecyclerView.ViewHolder(CardView(parent.context)) {}
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                (holder.itemView as CardView).apply { track = tracks[position]; onClick = { Playback.play(tracks, position) } }
            }
        }
    }

    private fun bindFooter(wrap: FrameLayout) {
        wrap.removeAllViews()
        val loading = Library.searching || Library.detail?.loading == true
        if (loading) {
            wrap.addView(TextView(this).apply { text = "Loading…"; setTextColor(C.onSurfaceVariant); textSize = 14f; gravity = Gravity.CENTER }, FrameLayout.LayoutParams(-1, -1))
        } else {
            wrap.addView(PillView(this).apply {
                label = "Load more"; icon = "plus"
                setOnClickListener { if (Library.detail != null) Library.detailLoadMore() else Library.loadMore() }
            }, FrameLayout.LayoutParams(-2, -2, Gravity.CENTER))
        }
    }

    private fun bindTrack(row: RowView, t: Track, index: Int, item: Item.T? = null) {
        row.vid = t.vid
        row.title = t.title
        row.sub = if (t.duration > 0) "${t.artist}  ·  ${Ui.fmtTime(t.duration)}" else t.artist
        row.thumbKey = "t:" + t.vid
        row.thumbUrl = Thumbs.trackUrl(t.vid)
        row.badge = ""
        row.icons = if (Ui.compact) arrayOf("heart", "more") else arrayOf("heart", "download", "more")
        row.activeProvider = { Playback.currentVid == t.vid && (tab != Tab.QUEUE || Playback.player?.currentMediaItemIndex == index) }
        row.selecting = selecting && canSelect()
        row.checked = row.selecting && t.vid in selected
        row.onRow = {
            val c = item?.ctx
            if (selecting) toggleSel(t.vid)
            else if (c != null) { hideKeyboard(); Playback.play(c, item.ctxIndex) } else onTrackClicked(index)
        }
        // queue rows: long-press drags; lists you can clear: long-press starts selecting; elsewhere: the song menu
        row.onLongRow = when {
            tab == Tab.QUEUE -> null
            selecting -> null
            canSelect() -> { { startSelect(t.vid) } }
            else -> { { showTrackMenu(row, t, index) } }
        }
        row.onButton = { _, name ->
            when (name) {
                "heart" -> Store.toggleLike(t)
                "download" -> toggleDownload(t)
                "more" -> showTrackMenu(row, t, index)
            }
        }
        row.invalidate()
    }

    private fun bindRemote(row: RowView, ref: PlaylistRef) {
        row.vid = null
        row.title = ref.name
        row.sub = listOf(ref.uploader, if (ref.count > 0) "${ref.count} videos" else "").filter { it.isNotEmpty() }.joinToString("  ·  ")
        row.thumbKey = "p:" + ref.url
        row.thumbUrl = ref.thumbUrl
        row.badge = if (ref.count > 0) ref.count.toString() else ""
        row.icons = arrayOf("bookmark")
        row.activeProvider = null
        row.onRow = { Library.openRemote(ref) }
        row.onLongRow = null
        row.onButton = { _, _ -> saveRemote(ref.name, ref.url) }
        row.invalidate()
    }

    private fun bindSaved(row: RowView, p: SavedPlaylist) {
        row.vid = null
        row.title = p.name
        row.sub = (if (p.tracks.size == 1) "1 song" else "${p.tracks.size} songs") + if (p.source != null) "  ·  from YouTube" else ""
        val cover = p.tracks.firstOrNull()?.vid
        row.thumbKey = if (cover != null) "t:$cover" else ""
        row.thumbUrl = if (cover != null) Thumbs.trackUrl(cover) else ""
        row.badge = p.tracks.size.toString()
        row.icons = arrayOf("play", "more")
        row.activeProvider = null
        row.selecting = selecting && canSelect()
        row.checked = row.selecting && p.id in selected
        row.onRow = { if (selecting) toggleSel(p.id) else { savedId = p.id; rebuild(); list.scrollToPosition(0) } }
        row.onLongRow = if (selecting) null else { { startSelect(p.id) } }
        row.onButton = { _, name ->
            if (name == "play") Playback.play(p.tracks, 0) else showSavedMenu(row, p)
        }
        row.invalidate()
    }

    private fun onTrackClicked(index: Int) {
        hideKeyboard()
        if (tab == Tab.QUEUE) { Playback.skipTo(index); return }
        val tracks = currentTracks()
        if (index in tracks.indices) Playback.play(tracks, index)
    }

    private fun toggleDownload(t: Track) {
        when {
            Downloads.isDone(t.vid) -> confirm("Delete the downloaded copy of “${t.title}”?") { Downloads.delete(t.vid) }
            Downloads.progress(t.vid) != null -> Downloads.cancel(t.vid)
            else -> { Downloads.enqueue(this, listOf(t)); toast("Downloading…") }
        }
    }

    // ---------------------------------------------------------------- swipe + drag in lists
    private val touchHelper = ItemTouchHelper(object : ItemTouchHelper.Callback() {
        override fun getMovementFlags(rv: RecyclerView, vh: RecyclerView.ViewHolder): Int {
            if (selecting || items.getOrNull(vh.bindingAdapterPosition) !is Item.T) return 0
            return when {
                tab == Tab.QUEUE -> makeMovementFlags(ItemTouchHelper.UP or ItemTouchHelper.DOWN, ItemTouchHelper.LEFT or ItemTouchHelper.RIGHT)
                tab == Tab.PLAYLISTS && savedId != null -> makeMovementFlags(0, ItemTouchHelper.LEFT or ItemTouchHelper.RIGHT)
                else -> makeMovementFlags(0, ItemTouchHelper.RIGHT)          // swipe right: play next
            }
        }
        override fun isLongPressDragEnabled() = tab == Tab.QUEUE && !selecting
        override fun onMove(rv: RecyclerView, vh: RecyclerView.ViewHolder, target: RecyclerView.ViewHolder): Boolean {
            val from = vh.bindingAdapterPosition; val to = target.bindingAdapterPosition
            if (items.getOrNull(to) !is Item.T) return false
            val m = items.toMutableList(); m.add(to, m.removeAt(from)); items = m
            adapter.notifyItemMoved(from, to)
            Playback.move(from, to)
            return true
        }
        override fun onSelectedChanged(vh: RecyclerView.ViewHolder?, state: Int) {
            super.onSelectedChanged(vh, state)
            if (state == ItemTouchHelper.ACTION_STATE_DRAG) { reordering = true; vh?.itemView?.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS) }
        }
        override fun clearView(rv: RecyclerView, vh: RecyclerView.ViewHolder) {
            super.clearView(rv, vh)
            if (reordering) { reordering = false; scheduleRefresh() }
        }
        override fun onSwiped(vh: RecyclerView.ViewHolder, direction: Int) {
            val pos = vh.bindingAdapterPosition
            val t = (items.getOrNull(pos) as? Item.T)?.track ?: return
            val remove = tab == Tab.QUEUE || (tab == Tab.PLAYLISTS && savedId != null)
            if (remove) {
                items = items.toMutableList().also { it.removeAt(pos) }
                adapter.notifyItemRemoved(pos)
                if (tab == Tab.QUEUE) Playback.removeAt(pos) else savedId?.let { Store.removeFromPlaylist(it, pos) }
                toast("Removed")
            } else {
                Playback.playNext(t); toast("Playing next")
                adapter.notifyItemChanged(pos)
            }
        }
        private val bg = Paint(Paint.ANTI_ALIAS_FLAG)
        override fun onChildDraw(c: Canvas, rv: RecyclerView, vh: RecyclerView.ViewHolder, dX: Float, dY: Float, state: Int, active: Boolean) {
            if (state == ItemTouchHelper.ACTION_STATE_SWIPE && dX != 0f) {
                val v = vh.itemView
                val remove = (tab == Tab.QUEUE || (tab == Tab.PLAYLISTS && savedId != null))
                bg.color = if (remove) C.alpha(C.error, 70) else C.secondaryContainer
                val top = v.top + Ui.dp(2); val bottom = v.bottom - Ui.dp(2)
                if (dX > 0) { c.drawRoundRect(v.left + Ui.dp(4), top, v.left + dX, bottom, Ui.dp(16), Ui.dp(16), bg); Icons.draw(c, if (remove) "trash" else "queueNext", v.left + Ui.dp(34), v.top + v.height / 2f, Ui.dp(24), if (remove) C.error else C.onSecondaryContainer) }
                else { c.drawRoundRect(v.right + dX, top, v.right - Ui.dp(4), bottom, Ui.dp(16), Ui.dp(16), bg); Icons.draw(c, "trash", v.right - Ui.dp(34), v.top + v.height / 2f, Ui.dp(24), C.error) }
            }
            super.onChildDraw(c, rv, vh, dX, dY, state, active)
        }
    })

    // ---------------------------------------------------------------- menus & dialogs
    private fun trackUrl(t: Track) = "https://youtu.be/${t.vid}"

    private fun shareTrack(t: Track) {
        val i = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, "${t.title}\n${trackUrl(t)}")
        startActivity(Intent.createChooser(i, "Share"))
    }

    private fun copyLink(t: Track) {
        (getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("link", trackUrl(t)))
        toast("Link copied")
    }

    private fun startRadio(t: Track) {
        toast("Starting radio…")
        App.io.execute {
            try {
                val rel = Yt.related(t.vid)
                App.main.post { if (rel.isEmpty()) toast("No similar songs found") else Playback.play(listOf(t) + rel, 0) }
            } catch (e: Throwable) {
                Crash.record("radio ${t.vid}", e)
                App.main.post { toast("Couldn't start radio") }
            }
        }
    }

    private fun showTrackMenu(anchor: View, t: Track, index: Int) {
        val m = PopupMenu(this, anchor, Gravity.END)
        val liked = t.vid in Store.likedSet
        m.menu.add(0, 1, 0, "Play next")
        m.menu.add(0, 2, 1, "Add to queue")
        m.menu.add(0, 3, 2, "Start radio")
        m.menu.add(0, 4, 3, "Add to playlist…")
        m.menu.add(0, 5, 4, if (liked) "Remove from liked" else "Add to liked")
        m.menu.add(0, 6, 5, if (Downloads.isDone(t.vid)) "Delete download" else if (Downloads.progress(t.vid) != null) "Cancel download" else "Download")
        m.menu.add(0, 7, 6, "Share…")
        m.menu.add(0, 8, 7, "Copy link")
        if (t.artist.isNotBlank()) m.menu.add(0, 11, 7, "More from ${t.artist}")
        if (tab == Tab.QUEUE) m.menu.add(0, 9, 8, "Remove from queue")
        if (tab == Tab.PLAYLISTS && savedId != null) m.menu.add(0, 10, 8, "Remove from this playlist")
        m.setOnMenuItemClickListener { mi ->
            when (mi.itemId) {
                1 -> { Playback.playNext(t); toast("Playing next") }
                2 -> { Playback.enqueue(listOf(t)); toast("Added to queue") }
                3 -> startRadio(t)
                4 -> addToPlaylistDialog(listOf(t))
                5 -> Store.toggleLike(t)
                6 -> toggleDownload(t)
                7 -> shareTrack(t)
                8 -> copyLink(t)
                9 -> Playback.removeAt(index)
                11 -> { setSearchText(t.artist); runSearch() }
                10 -> savedId?.let { Store.removeFromPlaylist(it, index) }
            }
            true
        }
        m.show()
    }

    private fun showSavedMenu(anchor: View, p: SavedPlaylist) {
        val m = PopupMenu(this, anchor, Gravity.END)
        m.menu.add(0, 1, 0, "Shuffle play")
        m.menu.add(0, 2, 1, "Add to queue")
        m.menu.add(0, 3, 2, "Download all")
        m.menu.add(0, 4, 3, "Rename")
        m.menu.add(0, 5, 4, "Delete")
        m.setOnMenuItemClickListener { mi ->
            when (mi.itemId) {
                1 -> Playback.play(p.tracks, 0, shuffle = true)
                2 -> { Playback.enqueue(p.tracks); toast("Added to queue") }
                3 -> { Downloads.enqueue(this, p.tracks); toast("Downloading ${p.tracks.size} songs") }
                4 -> promptName("Rename playlist", p.name) { n -> Store.renamePlaylist(p.id, n) }
                5 -> confirm("Delete “${p.name}”?") { Store.deletePlaylist(p.id) }
            }
            true
        }
        m.show()
    }

    private fun showAppMenu(anchor: View) {
        val m = PopupMenu(this, anchor, Gravity.END)
        m.menu.add(0, 8, 0, "Settings")
        m.menu.add(0, 4, 1, "Check for updates")
        m.setOnMenuItemClickListener { mi ->
            when (mi.itemId) {
                8 -> showSettings()
                4 -> { toast("Checking…"); Updater.check(manual = true) { msg -> toast(msg) } }
            }
            true
        }
        m.show()
    }

    // ---------------------------------------------------------------- settings screen
    private fun showSettings() {
        val dlg = android.app.Dialog(this, android.R.style.Theme_DeviceDefault_NoActionBar)
        val page = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(C.surface) }
        val scroll = ScrollView(this)
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, 0, 0, Ui.dpi(24)) }
        scroll.addView(body)

        val top = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(Ui.dpi(8), Ui.dpi(8), Ui.dpi(16), Ui.dpi(8)) }
        top.addView(IconBtn(this).apply { icon = "back"; tint = C.onSurface; setOnClickListener { dlg.dismiss() } }, LinearLayout.LayoutParams(Ui.dpi(44), Ui.dpi(44)))
        top.addView(TextView(this).apply { text = "Settings"; setTextColor(C.onSurface); textSize = 20f; typeface = Ui.medium; setPadding(Ui.dpi(8), 0, 0, 0) })
        page.addView(top)
        page.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))

        fun section(t: String) = body.addView(TextView(this).apply {
            text = t; setTextColor(C.primary); textSize = 13f; typeface = Ui.medium
            setPadding(Ui.dpi(20), Ui.dpi(20), Ui.dpi(20), Ui.dpi(6))
        })
        fun row(title: String, sub: () -> String, onClick: ((TextView) -> Unit)?): TextView {
            val sv = TextView(this).apply { setTextColor(C.onSurfaceVariant); textSize = 13f; text = sub() }
            val box = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL; setPadding(Ui.dpi(20), Ui.dpi(12), Ui.dpi(20), Ui.dpi(12))
                addView(TextView(this@MainActivity).apply { text = title; setTextColor(C.onSurface); textSize = 16f })
                addView(sv)
                if (onClick != null) { isClickable = true; setOnClickListener { onClick(sv) } }
            }
            body.addView(box, LinearLayout.LayoutParams(-1, -2))
            return sv
        }
        fun toggle(title: String, sub: String, get: () -> Boolean, set: (Boolean) -> Unit) {
            val sw = Switch(this).apply { isChecked = get(); setOnCheckedChangeListener { _, on -> set(on) } }
            val box = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(Ui.dpi(20), Ui.dpi(12), Ui.dpi(20), Ui.dpi(12))
                val tx = LinearLayout(this@MainActivity).apply {
                    orientation = LinearLayout.VERTICAL
                    addView(TextView(this@MainActivity).apply { text = title; setTextColor(C.onSurface); textSize = 16f })
                    addView(TextView(this@MainActivity).apply { text = sub; setTextColor(C.onSurfaceVariant); textSize = 13f })
                }
                addView(tx, LinearLayout.LayoutParams(0, -2, 1f))
                addView(sw)
                isClickable = true; setOnClickListener { sw.toggle() }
            }
            body.addView(box, LinearLayout.LayoutParams(-1, -2))
        }

        section("Appearance")
        row("Theme", { C.names.first { it.first == C.name }.second }) { sv -> chooseTheme(); sv.text = C.names.first { it.first == C.name }.second }

        row("Show thumbnails", { if (Settings.thumbnails) "On" else "Off (saves data and memory)" }) { sv ->
            Settings.thumbnails = !Settings.thumbnails; sv.text = if (Settings.thumbnails) "On" else "Off (saves data and memory)"; Thumbs.clearDisk()
        }
        row("Open on", { startLabel() }) { sv ->
            val keys = listOf("LAST") + Tab.values().map { it.name }
            val names = (listOf("Last used tab") + Tab.values().map { it.label }).toTypedArray()
            AlertDialog.Builder(this).setTitle("Open on").setSingleChoiceItems(names, keys.indexOf(Settings.startTab).coerceAtLeast(0)) { d, i ->
                Settings.startTab = keys[i]; sv.text = startLabel(); d.dismiss()
            }.show()
        }

        section("Playback")
        row("Audio quality", { qualityLabel() }) { sv ->
            val names = arrayOf("Data saver (~48 kbps)", "Normal (~128 kbps)", "Best available")
            AlertDialog.Builder(this).setTitle("Audio quality").setSingleChoiceItems(names, Settings.audioQuality) { d, i ->
                Settings.audioQuality = i; sv.text = qualityLabel(); d.dismiss(); toast("Applies to songs that start from now on")
            }.show()
        }
        toggle("Keep screen on", "While the full-screen player is open", { Settings.keepScreenOn }, { Settings.keepScreenOn = it })

        section("Downloads")
        toggle("Wi-Fi only", "Don't download over mobile data or a metered connection", { Settings.wifiOnlyDownloads }, { Settings.wifiOnlyDownloads = it })
        row("Storage used", { Ui.fmtBytes(Downloads.totalBytes()) }, null)
        row("Delete all downloads", { "Frees up the space used by saved songs" }) {
            val n = Downloads.tracks().size
            if (n == 0) toast("No downloads") else
                confirmTwice("Delete all $n downloads (${Ui.fmtBytes(Downloads.totalBytes())})?", "Every downloaded song will be removed from this device. It can't be undone.") { Downloads.deleteAll(); toast("Downloads deleted") }
        }

        section("Updates")
        toggle("Check automatically", "Look for a new version when the app starts", { Settings.autoUpdateCheck }, { Settings.autoUpdateCheck = it })
        row("Check now", { "Version ${Updater.currentName()} (${Updater.currentCode()})" }) { toast("Checking…"); Updater.check(manual = true) { msg -> toast(msg) } }
        row("Update server", { Updater.baseUrl.ifEmpty { "Found automatically on your network" } }) { sv ->
            promptName("Update server address", Updater.baseUrl) { u -> Updater.baseUrl = u; sv.text = u.ifEmpty { "Found automatically on your network" }; toast("Saved. Checking…"); Updater.check(manual = true) { msg -> toast(msg) } }
        }

        section("Privacy & data")
        row("Clear search history", { "${Store.history.size} recent searches" }) { sv -> Store.clearHistory(); sv.text = "0 recent searches" }
        row("Clear play history", { "Resets “Jump back in” and the For you page" }) { sv ->
            confirm("Clear your recently played list and recommendations?") { Store.clearRecent(); ForYou.refresh(); sv.text = "Cleared" }
        }
        row("Clear picture cache", { Ui.fmtBytes(Thumbs.diskBytes()) }) { sv -> Thumbs.clearDisk(); sv.text = Ui.fmtBytes(Thumbs.diskBytes()) }

        section("About")
        row("Last error", { "Shows the most recent crash or playback error" }) { showCrash() }
        row("YT Music", { "Version ${Updater.currentName()} (${Updater.currentCode()})" }, null)

        dlg.setContentView(page)
        dlg.show()
    }

    private fun qualityLabel() = arrayOf("Data saver (~48 kbps)", "Normal (~128 kbps)", "Best available")[Settings.audioQuality]
    private fun startLabel() = if (Settings.startTab == "LAST") "Last used tab" else Tab.values().firstOrNull { it.name == Settings.startTab }?.label ?: "Last used tab"

    private fun chooseTheme() {
        val names = C.names.map { it.second }.toTypedArray()
        val cur = C.names.indexOfFirst { it.first == C.name }
        AlertDialog.Builder(this).setTitle("Theme").setSingleChoiceItems(names, cur) { d, which ->
            getSharedPreferences("ui", MODE_PRIVATE).edit().putString("theme", C.names[which].first).apply()
            C.apply(C.names[which].first)
            d.dismiss()
            recreate()
        }.show()
    }

    private fun showCrash() {
        val text = Crash.last() ?: "No errors recorded."
        AlertDialog.Builder(this).setTitle("Last error").setMessage(text)
            .setPositiveButton("Close", null).setNeutralButton("Clear") { _, _ -> Crash.clear() }.show()
    }

    private fun addToPlaylistDialog(tracks: List<Track>) {
        val names = ArrayList<String>()
        names += "New playlist…"
        Store.playlists.forEach { names += it.name }
        AlertDialog.Builder(this).setTitle("Add to playlist").setItems(names.toTypedArray()) { _, which ->
            if (which == 0) promptName("New playlist", "") { n ->
                val p = Store.createPlaylist(n, tracks)
                toast("Added to “${p.name}”")
            } else {
                val p = Store.playlists[which - 1]
                val n = Store.addToPlaylist(p.id, tracks)
                toast(if (n == 0) "Already in “${p.name}”" else "Added to “${p.name}”")
            }
        }.show()
    }

    private fun promptName(title: String, initial: String, onOk: (String) -> Unit) {
        val input = EditText(this).apply {
            setText(initial); setSelection(initial.length); setSingleLine()
            setTextColor(C.onSurface); setHintTextColor(C.onSurfaceVariant); hint = "Name"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
        }
        val pad = Ui.dpi(20)
        val wrap = FrameLayout(this).apply { setPadding(pad, Ui.dpi(8), pad, 0); addView(input) }
        val dlg = AlertDialog.Builder(this).setTitle(title).setView(wrap)
            .setPositiveButton("OK") { _, _ -> onOk(input.text.toString()) }
            .setNegativeButton("Cancel", null).create()
        dlg.window?.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE)
        dlg.show()
    }

    private fun confirm(msg: String, onYes: () -> Unit) {
        AlertDialog.Builder(this).setMessage(msg).setPositiveButton("Yes") { _, _ -> onYes() }.setNegativeButton("Cancel", null).show()
    }

    private fun showSleepDialog() {
        val opts = arrayOf("15 minutes", "30 minutes", "1 hour", "2 hours", "Off")
        val mins = intArrayOf(15, 30, 60, 120, 0)
        AlertDialog.Builder(this).setTitle("Sleep timer").setItems(opts) { _, i ->
            Playback.setSleep(mins[i])
            toast(if (mins[i] == 0) "Sleep timer off" else "Music will pause in ${opts[i]}")
        }.show()
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
}
