
read catalog

mediaItem to list(hash + ts): n to n
hash_2_media

match
1. input buffer + i_match_time

2.
for slice of buffer:
    cal (hash, ts) of this buffer
collect intput_list(hash + ts)

3. do matching

// add candidate
for every i_hash, i_ts in input:
    if hash_2_signaturelist not contains i_hash:
        continue

    t_signature_ts_list = hash_2_signature_ts_list[i_hash]  // hash hit
    for every signature, t_ts in t_signature_ts_list:  // t_ts is ts of hash
        ts_offset = i_ts - t_ts

        if sizeof(signature_2_session_cnt[signature]) > 3:
            continue  // quata is full for this signature

        candidate_session_key = aproximate(ts_offset) | signature
        if candidate_session_map not contains candidate_session_key:
            candidate_session = { match_cnt = 1, media_item, ts_offset, t_ts, i_ts }
            candidate_session_map[candidate_session_key] = candidate_session
            signature_2_session[signature] += 1
        else
            candidate_session = candidate_session_map[candidate_session_key]
            if candidate_session.is_notified:
                continue
            if  candidate_session.t_last_match_hash_ts > t_ts:  // ignore for now, could check count and add a new candidate session. if add new one, need to add info to make unique session key
                continue
            
            candidate_session.match_cnt += 1
            candidate_session.t_last_match_hash_ts = t_ts

// evaluete
match_result_list = []
decay_session_key_list = []
for every session_key, candidate_session in candidate_session_map
    score = evaluete(candidate_session)
    if is_enough_to_notify(score):
        match_result = { score, candidate_session.media_item }
        match_result_list.add(match_result)

        candidate_session_map[session_key].is_notified = true

        if candidate_session.t_last_match_hash_ts + 3000 < i_match_time:
            decay_session_key.add(session_key)

for match_result in match_result_list:
    notify_result(match_result)


// roll

for decay_session_key in decay_session_key_list:
    candidate_session_map.erase(decay_session_key)
    signature = get_sig(decay_session_key)
    signature_2_session_cnt[signature] -= 1

