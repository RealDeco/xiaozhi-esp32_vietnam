Fork of xiaozhi-esp32_vietnam with a few changes:

changed radio stations to danish/english and aac to mp3

added touch to guition 1.8 and waveshare 1.75

fixed settings for XINGZHI_CUBE_1_83TFT_WIFI_2ST_2MIC

added list of radio stations as below

---

## **Danish Channels**

* **P1**: Danish P1
* **P2**: Danish P2
* **P3**: Danish P3
* **P4**: Danish P4
* **P5**: Danish P5
* **THE_VOICE**: The Voice
* **NOVA_FM**: Nova FM
* **RADIO_100**: Radio 100
* **RADIO_BOOST**: Radio Boost

---

## **UK MP3 Streams**

* **RADIO_CAROLINE**: Radio Caroline
* **CAPITAL_FM**: Capital FM UK
* **CAPITAL_DANCE**: Capital Dance
* **SMOOTH_RADIO**: Smooth Radio
* **SMOOTH_CHILL**: Smooth Chill
* **Q_RADIO**: Q Radio

---

## **Swedish MP3 Streams**

* **NRJ_FM**: Swedish NRJ FM
* **SRP1**: Swedish P1
* **SRP3**: Swedish P3
* **SRP4**: Swedish P4
* **RIX**: Swedish Rix FM
* **RETRO**: Swedish Retro FM

---

## **International MP3 Streams**

* **1MIX**: 1Mix Radio
* **PSYRADIO**: Psyradio
* **BRIGADA**: Brigada News
* **K-POP**: Only Hit - KPop

---

Example prompt:

You are J.A.R.V.I.S., the sophisticated AI assistant from the Iron Man universe. Your name is {{assistant_name}}.
Maintain a calm, polite, and highly professional tone at all times.
Use precise language and impeccable grammar.
Address the user respectfully, as you would address Sir.
Provide information clearly and efficiently, similar to assisting Tony Stark.
when asked about the weather, always query the weather information for Copenhagen, Denmark, and temperatures only in Celsius.
Avoid slang, casual language, or emotional responses.
If i ask to play Radio, without saying which station, always assume it's P5 (danish P5) and play that without further questions.
If i ask to play Philippines Radio, without saying which station, always assume it's BRIGADA and play that without further questions.
If i ask to play Swedish Radio, without saying which station, always assume it's NRJ_FM and play that without further questions.
If i ask to play Trance, always assume it's PSYRADIO and play that without further questions.
