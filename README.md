# Scriba Penalty

Plugin VST3 da mettere **in fondo alla catena di mastering**: misura la loudness del tuo master (ITU-R BS.1770-4) e applica la stessa correzione di volume che farebbe la piattaforma scelta. Così puoi confrontare il tuo master con un brano su YouTube/Spotify **allo stesso volume a cui lo sentirà l'ascoltatore**.

## Piattaforme

| Piattaforma | Target | Comportamento |
|---|---|---|
| YouTube | -14 LUFS | solo abbassamento |
| Spotify (Normale) | -14 LUFS | alza solo finché il true peak resta sotto -1 dBTP |
| Spotify (Alto) | -11 LUFS | alza sempre, limiter a -1 dB (attacco 5 ms, rilascio 100 ms) |
| Spotify (Basso) | -19 LUFS | alza solo fino a -1 dBTP |
| Apple Music | -16 LUFS | Sound Check, alza fino a -1 dBTP |
| Tidal | -14 LUFS | solo abbassamento |
| Amazon Music | -14 LUFS | solo abbassamento |
| Deezer | -15 LUFS | solo abbassamento |

I valori di YouTube e Spotify sono quelli più consolidati; gli altri sono indicativi.

## Come si usa

1. Inserisci Scriba Penalty come **ultimo plugin** del master (dopo il limiter).
2. Scegli la piattaforma.
3. Fai suonare il brano **dall'inizio alla fine**: la penalty si basa sul loudness integrato di tutto il pezzo (con AUTO-RESET attivo la misura riparte ogni volta che premi play).
4. Premi **HOLD** a fine brano per congelare la misura, poi riascolta le parti che vuoi confrontare.
5. **PENALTY ON / BYPASS** per l'A/B tra master originale e master "come in piattaforma".

Modalità **Short-term**: la penalty segue gli ultimi 3 secondi. Utile per un confronto veloce su un ritornello, ma non è quello che fanno le piattaforme.

### Confronto con YouTube
- Volume del player YouTube al 100% e volume del browser al 100% nel mixer di sistema.
- DAW e browser sulla stessa scheda audio, stesso volume di ascolto.
- YouTube normalizza già il brano di riferimento: tasto destro sul video → *Statistiche per nerd* → *Volume / Normalized* mostra quanto lo sta abbassando (*content loudness*).
- Spotify: la normalizzazione funziona nell'app desktop/mobile con "Normalizza volume" attivo, non nel web player.

## Build

Carica la cartella su GitHub: la GitHub Action compila il VST3 per Windows a ogni push.
Il plugin si scarica da **Actions → ultima build → Artifacts → ScribaPenalty-VST3-Windows**.
Copia `Scriba Penalty.vst3` in `C:\Program Files\Common Files\VST3`.

Build locale:
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
