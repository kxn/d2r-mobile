# Bounded original-input historical camp gate route

Successor of b166b10, preserving runtime773/corrected11605ASTC6/system/sharedOFF/legacyOFF/cbd8/UI, original2campwalks, resourcecp/CAS/installer, meminfo/showmap and originalfreshsave2stop/fullrestoration. No runtime/resource/driver or CPU/GPUcapture changes.

The nearestroomAABB point usedbyb166 isnotnecessarilytraversable; actual15s attemptstoppedarea1 at5234/6282, saved/restored withoutgamecrash. Historicalnormalinput logs show three independent routes east5265/6288 then gate5267/6275 actuallyenteringarea2 around5267/6278. Evidence: .local/logs/mali773-ASTC6-reported-exit-history-route-audit-agent.json.

The onlyroute change is east5265/6288 radius4/15s, then gate5267/6275 radius.5/15s. Both use the byte-identical b166 thinoriginaljoystickhelper withownedPID/stop-on-anyreadyareachange andUPfinally. Firstlegtruearea2 isaccepted immediately; anyunexpectedarea ormissfails. Firstlegcoordinatearrival mustactually bewithinradius4. No newpathfinding, playerstatewrite, teleport orautomaticcombat. Actualnormalprogressis saved andneverrolledback.

Defaultoffline, rootreviewedinputrequired. HistoricalsuccessisnotcurrentASTC6fieldqualification; actualarea2/HUD isrequired. Noheat/6GB/allscene/combatclaim.
