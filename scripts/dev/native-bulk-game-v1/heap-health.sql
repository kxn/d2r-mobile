SELECT name, idx, value, severity FROM stats WHERE name GLOB 'heapprofd*' OR (severity='error' AND value!=0) ORDER BY name,idx;
