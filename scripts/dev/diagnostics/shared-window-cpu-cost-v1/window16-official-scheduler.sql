-- Existing official ATrace + scheduler/frequency; official SPAN_JOIN interval intersections.
-- Each comm/name is an independent nonoverlapping scope timeline. Never sum nested names.
INCLUDE PERFETTO MODULE linux.cpu.frequency;
CREATE PERFETTO TABLE target_threads AS
SELECT DISTINCT t.utid,t.tid,CASE WHEN EXISTS(SELECT 1 FROM thread_track tr JOIN slice s ON s.track_id=tr.id WHERE tr.utid=t.utid AND s.name='NVN command replay') THEN 'nvn-translate' ELSE t.name END AS comm,p.upid,p.pid,p.name AS process
FROM thread t JOIN process p USING(upid) WHERE p.pid=30007 AND (t.name='dm-gpu-complete' OR EXISTS(SELECT 1 FROM thread_track tr JOIN slice s ON s.track_id=tr.id WHERE tr.utid=t.utid AND s.name IN('NVN command replay','Vulkan present call','Vulkan swapchain acquire','NVN CPU waits guest GPU fence')));
CREATE PERFETTO TABLE operations AS
SELECT s.id,s.ts,s.dur,s.name,t.utid,t.tid,t.comm,
dense_rank() OVER (ORDER BY t.utid,s.name) AS scope_id
FROM slice s JOIN thread_track tt ON tt.id=s.track_id JOIN target_threads t ON t.utid=tt.utid WHERE s.dur>0;
CREATE PERFETTO TABLE scope_types AS SELECT DISTINCT scope_id,utid,tid,comm,name FROM operations;
CREATE PERFETTO TABLE scoped_sched AS
SELECT x.scope_id,q.ts,q.dur,q.cpu FROM scope_types x JOIN sched_slice q USING(utid) WHERE q.dur>0;
CREATE VIRTUAL TABLE operation_running USING SPAN_JOIN(operations PARTITIONED scope_id, scoped_sched PARTITIONED scope_id);
CREATE PERFETTO TABLE running_frequency_keys AS SELECT *,scope_id*4096+cpu AS freq_key FROM operation_running;
CREATE PERFETTO TABLE scoped_frequency AS
SELECT x.scope_id*4096+f.cpu AS freq_key,f.ts,f.dur,f.freq
FROM scope_types x CROSS JOIN cpu_frequency_counters f WHERE f.dur>0;
CREATE VIRTUAL TABLE operation_frequency USING SPAN_JOIN(running_frequency_keys PARTITIONED freq_key, scoped_frequency PARTITIONED freq_key);
CREATE PERFETTO TABLE scoped_states AS
SELECT x.scope_id,q.ts,q.dur,q.state,q.io_wait,q.blocked_function FROM scope_types x JOIN thread_state q USING(utid) WHERE q.dur>0;
CREATE VIRTUAL TABLE operation_states USING SPAN_JOIN(operations PARTITIONED scope_id, scoped_states PARTITIONED scope_id);
SELECT * FROM target_threads ORDER BY comm;
SELECT comm,name,count(*) AS count,avg(dur)/1e6 AS mean_wall_ms,sum(dur)/1e6 AS total_wall_ms,max(dur)/1e6 AS max_wall_ms
FROM operations GROUP BY comm,name ORDER BY comm,total_wall_ms DESC;
SELECT a.comm,a.name,a.count,a.mean_wall_ms,r.running_ms,r.running_ms/a.count AS mean_running_ms,
f.covered_ms,100.0*f.covered_ms/r.running_ms AS frequency_coverage_pct,f.weighted_mhz
FROM (SELECT comm,name,count(*) AS count,avg(dur)/1e6 AS mean_wall_ms FROM operations GROUP BY comm,name) a
LEFT JOIN (SELECT comm,name,sum(dur)/1e6 AS running_ms FROM operation_running GROUP BY comm,name) r USING(comm,name)
LEFT JOIN (SELECT comm,name,sum(dur)/1e6 AS covered_ms,sum(dur*1.0*freq)/sum(dur)/1000 AS weighted_mhz FROM operation_frequency GROUP BY comm,name) f USING(comm,name)
ORDER BY a.comm,a.name;
SELECT comm,name,cpu,sum(dur)/1e6 AS running_ms FROM operation_running GROUP BY comm,name,cpu ORDER BY comm,name,cpu;
SELECT comm,name,state,io_wait,blocked_function,sum(dur)/1e6 AS state_ms FROM operation_states GROUP BY comm,name,state,io_wait,blocked_function ORDER BY comm,name,state;
CREATE PERFETTO TABLE whole_running AS
SELECT t.comm,t.utid,q.cpu,q.ts,q.dur FROM target_threads t JOIN sched_slice q USING(utid) WHERE q.dur>0;
CREATE PERFETTO TABLE whole_frequency_keys AS SELECT *,utid*4096+cpu AS freq_key FROM whole_running;
CREATE PERFETTO TABLE whole_scoped_frequency AS
SELECT t.utid*4096+f.cpu AS freq_key,f.ts,f.dur,f.freq FROM target_threads t CROSS JOIN cpu_frequency_counters f WHERE f.dur>0;
CREATE VIRTUAL TABLE whole_frequency USING SPAN_JOIN(whole_frequency_keys PARTITIONED freq_key, whole_scoped_frequency PARTITIONED freq_key);
SELECT r.comm,r.cpu,r.running_ms,f.frequency_covered_ms,f.weighted_mhz FROM
(SELECT comm,cpu,sum(dur)/1e6 AS running_ms FROM whole_running GROUP BY comm,cpu) r
LEFT JOIN (SELECT comm,cpu,sum(dur)/1e6 AS frequency_covered_ms,sum(dur*1.0*freq)/sum(dur)/1000 AS weighted_mhz FROM whole_frequency GROUP BY comm,cpu) f USING(comm,cpu)
ORDER BY r.comm,r.cpu;
SELECT name,idx,value FROM stats WHERE value!=0 AND (name GLOB '*overrun*' OR name GLOB '*lost*' OR name GLOB '*parse_failure*');
SELECT t.comm,s.name,s.dur,count(*) AS excluded FROM slice s JOIN thread_track tt ON tt.id=s.track_id JOIN target_threads t ON t.utid=tt.utid WHERE s.dur<=0 GROUP BY t.comm,s.name,s.dur;
-- Validate SPAN_JOIN assumptions: same-scope overlap would invalidate attribution.
SELECT a.comm,a.name,count(*) AS overlapping_pairs FROM operations a JOIN operations b ON a.scope_id=b.scope_id AND a.id<b.id AND a.ts<b.ts+b.dur AND b.ts<a.ts+a.dur GROUP BY a.comm,a.name;


SELECT 'trace_bounds' AS section,start_ts,end_ts,(end_ts-start_ts)/1e9 AS seconds FROM trace_bounds;
SELECT 'target_thread_states' AS section,t.tid,t.comm,q.state,q.io_wait,q.blocked_function,
sum(max(0,min(q.ts+q.dur,b.end_ts)-max(q.ts,b.start_ts)))/1e6 AS state_ms
FROM target_threads t JOIN thread_state q USING(utid) CROSS JOIN trace_bounds b
WHERE q.dur>0 AND q.ts<b.end_ts AND q.ts+q.dur>b.start_ts
GROUP BY t.tid,t.comm,q.state,q.io_wait,q.blocked_function;
SELECT 'whole_PID_running' AS section,sum(q.dur)/1e6 AS running_ms
FROM sched_slice q JOIN thread t USING(utid) JOIN process p USING(upid)
WHERE p.pid=30007 AND q.dur>0;
SELECT 'health_full' AS section,name,idx,value,severity FROM stats WHERE value>0 AND severity IN ('error','data_loss');
SELECT 'frequency_tracks' AS section,tr.name,count(c.id) AS n FROM cpu_counter_track tr LEFT JOIN counter c ON c.track_id=tr.id GROUP BY tr.id;
