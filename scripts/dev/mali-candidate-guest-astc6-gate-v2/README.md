# Bounded original-input historical camp gate route

Successor of b166b10, preserving runtime773/corrected11605ASTC6/system/sharedOFF/legacyOFF/cbd8/UI, original2campwalks, resourcecp/CAS/installer, meminfo/showmap and originalfreshsave2stop/fullrestoration. No runtime/resource/driver or CPU/GPUcapture changes.

The nearestroomAABB point usedbyb166 isnotnecessarilytraversable; actual15s attemptstoppedarea1 at5234/6282, saved/restored withoutgamecrash. Historicalnormalinput logs show three independent routes east5265/6288 then gate5267/6275 actuallyenteringarea2 around5267/6278. Evidence: .local/logs/mali773-ASTC6-reported-exit-history-route-audit-agent.json.

The onlyroute change is east5265/6288 radius4/15s, then gate5267/6275 radius.5/15s. Both use the byte-identical b166 thinoriginaljoystickhelper withownedPID/stop-on-anyreadyareachange andUPfinally. Firstlegtruearea2 isaccepted immediately; anyunexpectedarea ormissfails. Firstlegcoordinatearrival mustactually bewithinradius4. No newpathfinding, playerstatewrite, teleport orautomaticcombat. Actualnormalprogressis saved andneverrolledback.

Defaultoffline, rootreviewedinputrequired. HistoricalsuccessisnotcurrentASTC6fieldqualification; actualarea2/HUD isrequired. Noheat/6GB/allscene/combatclaim.


截图证据后继（基于 exact0c19）：原标准 `screencap -p` 抽成同一helper，
已获准的 before-exit 及每个实际执行 east/gate 工具返回后各留一张PNG；
非零返回、工具异常也先尽力保存当时原屏幕，然后保留原路线失败。
图像SHA/大小/路径或截图错误写入reported-exit-result.json，不把截图失败
改成路线成功，也不凭图像存在宣称视觉通过。late HUD复用同helper，
失败只记录附属screenshot_errors，原save2/停机/5dir恢复继续；replacement
PID不截图且不save/stop，信号合作退出仍抛出。未改路线坐标/算法/时间预算、
原始资产/driver/raw验证、CPU/GPU工具，不访问设备。DRAFT仍false。
