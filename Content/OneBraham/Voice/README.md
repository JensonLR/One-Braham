# Content/OneBraham/Voice

* `DA_VoiceLines` - the line set metadata. The authoritative line table is
  `Source/OneBraham/Voice/OneBrahamVoiceLineSet.cpp`: every line has an id, a
  category, a priority and a cooldown, and the recording script in
  `Docs/14_Voice_Script.md` is generated from it.
* Waves go in at `Content/OneBraham/Voice/Waves/<VoiceSetId>/<LineId>.wav`, 24-bit
  48 kHz, mono, peak -3 dBFS. The runtime resamples for the 16 kHz voice codec;
  the recording must not be.
