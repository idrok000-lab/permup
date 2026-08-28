# permup
An alternative to sudo and pkexec
```markdown
# PERMUP – KOMPLETNA SPECYFIKACJA PROJEKTU

## 1. CEL

Zastąpić `sudo` i `pkexec` jednym narzędziem, które:
- Działa niezależnie od initu (systemd, OpenRC, runit, SysVinit)
- Używa grup jako jedynego nośnika uprawnień
- Rozróżnia komendy interaktywne (PTY) i statyczne (oneshot)
- Działa zarówno w CLI, jak i GUI
- Jest prosty, bezpieczny i zgodny z filozofią KISS
- **Nie działa zdalnie** – tylko lokalnie przez gniazdo Unix; zdalny dostęp odbywa się przez SSH, a następnie przez `permup`

## 2. ARCHITEKTURA OGÓLNA

```

┌─────────────────────────────────────────────────────────────┐
│                         SYSTEM                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐      Unix Socket      ┌──────────────┐  │
│  │   permup     │ ◄──────────────────► │   permupd    │  │
│  │   (klient)   │      /run/permup/    │   (demon)    │  │
│  └──────────────┘      socket           └──────────────┘  │
│         ▲                                       │          │
│         │                                       │          │
│  ┌──────┴──────┐                                │          │
│  │ permup-gui  │                                │          │
│  │  (klient)   │                                │          │
│  └─────────────┘                                │          │
│                                                 │          │
│                                          ┌──────▼──────┐   │
│                                          │  Dziecko    │   │
│                                          │  (proces)   │   │
│                                          │  runuser    │   │
│                                          │  + komenda  │   │
│                                          │  ; exit     │   │
│                                          └─────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘

```

## 3. KOMPONENTY

### 3.1. `permupd` – demon

- **Uruchamianie:** przez init systemu (systemd, OpenRC, runit, SysVinit) jako root
- **Funkcja:** nasłuchuje na gnieździe Unix, uwierzytelnia, sprawdza uprawnienia, tworzy dzieci
- **Model:** pojedynczy proces, `fork()` na każde połączenie, rodzic wraca do nasłuchu
- **Zbieranie zombie:** `SIGCHLD` lub `waitpid(-1, WNOHANG)` w pętli
- **Nowe żądanie:** `list_users` – zwraca listę dozwolonych `-u` i `-h` dla danego użytkownika
- **Zasada dla root:** Jeśli `-h root` → **pomiń wszystkie sprawdzenia** (uprawnienia, czas, blokady, powłoki)
- **Pliki konfiguracyjne:**
  - `/etc/permup.cfg` – uprawnienia (grupy, listy, blokady użytkowników)
  - `/etc/permdown.cfg` – timeouty dla komend
  - `/etc/permup/shell.rc` – wzorce promptów powłok
  - `/etc/permup.time` – ograniczenia czasowe dla grup
  - `/etc/permup.session` – czas ważności hasła (po stronie klienta)
  - `/etc/permup/permup.session` – rate limiting (po stronie klienta)

### 3.2. `permup` – klient CLI

- **Funkcja:** łączy się z `permupd`, pobiera listę dozwolonych użytkowników, wysyła komendę, hasło, odbiera PTY
- **Działanie:**
  - Sprawdza, czy ma terminal (`isatty()`)
  - Jeśli tak → tryb interaktywny (PTY)
  - **Najpierw** wysyła żądanie `list_users` do `permupd`
  - Otrzymuje listę dozwolonych `-u` i `-h`
  - Sprawdza, czy podany `-u` i `-h` są dozwolone
  - Jeśli nie → wyświetla odpowiedni komunikat błędu
  - Jeśli tak → sprawdza rate limiting (`/etc/permup/permup.session`)
    - Jeśli użytkownik jest zablokowany → wyświetla komunikat i kończy
    - Jeśli nie → przechodzi dalej
  - Sprawdza, czy `-h` to **root**
    - Jeśli **tak** → **zawsze pyta o hasło roota** (nie korzysta z `/etc/permup.session`)
    - Jeśli **nie** → sprawdza, czy istnieje ważna sesja dla pary `(-h, -u)` (zgodnie z `/etc/permup.session`)
      - Jeśli tak → wysyła zapamiętane hasło automatycznie
      - Jeśli nie → pyta o hasło interaktywnie
  - Wysyła właściwe żądanie do demona
  - Odbiera master PTY przez `SCM_RIGHTS`
  - Przejmuje terminal i obsługuje sesję
- **Składnia:**
  ```bash
  permup [-u użytkownik_docelowy] [-h użytkownik_uwierzytelniający] komenda [argumenty]
  permup -l
```

· -u – użytkownik, na którego ma zostać wykonana komenda (domyślnie: root)
· -h – użytkownik, którego hasło jest używane do uwierzytelnienia (domyślnie: root)
· -l – wyświetla listę dozwolonych użytkowników
· Hasło jest zawsze podawane interaktywnie po naciśnięciu Enter, tak jak w sudo i pkexec – bez wyjątków
· Przykład: permup -u janusz -h root htop → wykonaj jako janusz, uwierzytelnij hasłem roota

3.3. permup-gui – klient GUI

· Funkcja: łączy się z permupd, pobiera listę dozwolonych użytkowników, wyświetla okno dialogowe, wysyła komendę i hasło
· Działanie:
  · Wysyła żądanie list_users do permupd
  · Otrzymuje listę dozwolonych -u i -h
  · Wyświetla okno dialogowe z:
    · Listą rozwijaną dla -u (tylko dozwoleni użytkownicy)
    · Listą rozwijaną dla -h (tylko dozwoleni użytkownicy)
    · Polem na hasło
    · Przyciskiem OK/Anuluj
  · Po zatwierdzeniu sprawdza rate limiting (/etc/permup/permup.session)
    · Jeśli użytkownik jest zablokowany → wyświetla komunikat i kończy
    · Jeśli nie → przechodzi dalej
  · Sprawdza, czy -h to root
    · Jeśli tak → zawsze pyta o hasło roota (nie korzysta z /etc/permup.session)
    · Jeśli nie → sprawdza, czy istnieje ważna sesja dla pary (-h, -u)
      · Jeśli tak → wysyła zapamiętane hasło automatycznie
      · Jeśli nie → pyta o hasło w oknie dialogowym
  · Wysyła właściwe żądanie do permupd
  · Odbiera wyjście i wyświetla w oknie (lub w konsoli, z której zostało uruchomione)
· Zastosowanie: pliki .desktop, skróty w menu, Nautilus

4. KONFIGURACJA

4.1. /etc/permup.cfg – uprawnienia

Struktura:

```
grupa: {
    tryb: darklist | whitelist
    lista: {
        komenda1,
        komenda2,
        ...
    }
    except: {
        komenda3,
        komenda4,
        ...
    }
    blocked_users: {
        user1,
        user2,
        ...
    }
    allowed_users: {
        user1,
        user2,
        ...
    }
}
```

Zasady ogólne:

· darklist: {} → pusta = wszystko dozwolone
· whitelist: {} → pusta = nic nie jest dozwolone
· except zawsze odwraca regułę główną:
  · Dla darklist: except = dozwolone (mimo że na liście)
  · Dla whitelist: except = zablokowane (mimo że na liście)

Dopasowanie komend:

· Normalizacja flag (sortowanie alfabetyczne)
· rm -rf = rm -fr = rm -f -r = to samo
· Kanonizacja ścieżek (realpath)
· Dopasowanie podciągu: rm -r blokuje rm -rf, rm -fr, rm -r -f

Zasady dla użytkowników docelowych:

· blocked_users – czarna lista: ci użytkownicy są zablokowani
· allowed_users – biała lista: tylko ci użytkownicy są dozwoleni
· Jeśli obie sekcje są zdefiniowane → allowed_users ma pierwszeństwo, blocked_users jest ignorowane
· Jeśli żadna nie jest zdefiniowana → brak ograniczeń (można użyć każdego użytkownika docelowego)
· Jeśli sekcja jest pusta → brak ograniczeń

Przykład:

```
adm: {
    darklist: {
        rm -rf,
        reboot,
        shutdown
    },
    except: {
        rm -rf /var/cache/pacman,
        rm -rf /tmp
    },
    blocked_users: {
        root
    }
}

dev: {
    whitelist: {
        docker,
        systemctl,
        journalctl
    },
    except: {
        docker run --privileged,
        systemctl disable
    },
    allowed_users: {
        marek,
        ola
    }
}
```

Domyślna grupa:

· adm ma darklist: {} (wszystko dozwolone)

Łączenie grup:

· Użytkownik może należeć do wielu grup
· Uprawnienia sumują się (zbiór)
· Jeśli konflikt (dozwolone vs zablokowane) → dozwolone wygrywa

Użytkownicy bez grup:

· Brak uprawnień → odmowa

Użytkownik root:

· Nie podlega /etc/permup.cfg
· Ma wszystkie uprawnienia z góry – może wykonać każdą komendę jako każdy użytkownik

4.2. /etc/permdown.cfg – timeouty

Struktura:

```
default: 120s

pacman -Syu {
    1000s
}

systemctl restart nginx {
    30s
}

htop {
    20s
}
```

Zasady:

· default – używane, gdy komenda nie jest zdefiniowana
· Timeout dotyczy nawiązania połączenia z terminalem wywoławczym
· Jeśli w czasie timeoutu nie uda się nawiązać połączenia zwrotnego → dziecko zabija komendę i kończy się
· Dla GUI (brak terminala) → timeout działa jako maksymalny czas wykonania

4.3. /etc/permup/shell.rc – wzorce promptów powłok

Struktura:

```
[root@
[user@
$
#
>
%
bash-
zsh-
fish-
sh-
```

Zasady:

· Każda linia to wzorzec tekstowy, który może pojawić się na wyjściu PTY
· Jeśli którykolwiek wzorzec zostanie znaleziony w wyjściu komendy → uznajemy, że uruchomiona została powłoka
· Plik jest czytany przez dziecko przed przekazaniem PTY do klienta
· Admin może dodawać własne wzorce (np. [admin@, (venv) )
· Root (-h root) pomija ten test – może uruchamiać powłoki bez ograniczeń

4.4. /etc/permup.time – ograniczenia czasowe

Struktura:

```
grupa: {
    allowed: "08:00-18:00",
    days: "Mon-Fri"
}

dev: {
    allowed: "10:00-16:00",
    days: "Mon-Fri"
}

backup: {
    allowed: "00:00-23:59",
    days: "Mon-Sun"
}
```

Zasady:

· Jeśli grupa nie ma wpisu w /etc/permup.time → brak ograniczeń czasowych (domyślnie 24/7)
· Jeśli grupa ma wpis → permupd sprawdza bieżący czas i dzień tygodnia
· Jeśli aktualny czas mieści się w przedziale i dzień zgadza się → pozwala
· Jeśli nie → odmawia z komunikatem: "Uprawnienia czasowe dla tej grupy nie są dostępne w tym momencie."
· Jeśli użytkownik należy do wielu grup → którakolwiek grupa pozwala na dostęp w danym czasie → uprawnienie jest ważne
· Root (-h root) ignoruje /etc/permup.time – ograniczenia czasowe nie są sprawdzane

4.5. /etc/permup.session – czas ważności hasła (po stronie klienta)

Struktura:

```
# Czas w sekundach
adm: {
    session_time: 600
}

dev: {
    session_time: 300
}
```

Zasady:

· session_time – czas w sekundach, przez który klient (permup lub permup-gui) pamięta hasło dla konkretnej pary (-h, -u) i wysyła je automatycznie przy kolejnych wywołaniach z tą samą parą
· Jeśli grupa nie ma wpisu → za każdym razem pytaj o hasło (domyślnie: brak pamięci)
· Jeśli session_time = 0 → wyłączone (zawsze pytaj)
· Jeśli session_time > 0 → klient zapamiętuje hasło na ten czas (liczone od ostatniego uwierzytelnienia dla tej pary)
· Każda para (-h, -u) ma własną, niezależną sesję
· Zmiana pary (-h, -u) → stara sesja jest zamykana, otwierana jest nowa (po podaniu prawidłowego hasła dla nowej pary)
· Działa wyłącznie po stronie klienta – permupd nie ma pojęcia o sesji
· Root (-h root) nie korzysta z /etc/permup.session – zawsze pyta o hasło
· Sesje są niezależne dla każdego użytkownika wywołującego – Iksiński i Kowalski mają własne, oddzielne zbiory sesji

4.6. /etc/permup/permup.session – rate limiting (po stronie klienta)

Struktura:

```
# Ustawienia domyślne
default_n: 3
default_x: 30

# Ustawienia dla konkretnych użytkowników (nadpisują default)
marek {
    n: 5
    x: 60
}

janusz {
    n: 2
    x: 10
}
```

Zasady:

· default_n – domyślna liczba nieudanych prób przed blokadą (dla wszystkich użytkowników)
· default_x – domyślny czas blokady w sekundach (dla wszystkich użytkowników)
· Ustawienia dla konkretnego użytkownika (np. marek { }) nadpisują wartości domyślne
· n – liczba nieudanych prób uwierzytelnienia (hasło) przed zablokowaniem
· x – czas w sekundach, przez który użytkownik jest blokowany po przekroczeniu n
· Licznik jest resetowany po pomyślnym uwierzytelnieniu lub po upływie x
· Dotyczy wyłącznie strony klienta – permupd nie ma pojęcia o blokadzie
· Root (-h root) nie podlega rate limitingowi – zawsze może próbować

5. PRZEPŁYW DZIAŁANIA

5.1. Start systemu

1. Init uruchamia permupd jako root
2. permupd otwiera /run/permup/socket (uprawnienia: 0600, tylko root)
3. permupd wchodzi w pętlę nasłuchiwania

5.2. Wywołanie przez użytkownika (CLI)

1. Użytkownik wpisuje: permup htop
2. permup łączy się z /run/permup/socket
3. permup wysyła żądanie list_users do permupd
4. permupd sprawdza grupy użytkownika wywołującego i zwraca listę dozwolonych -u i -h
5. permup sprawdza, czy podany -u (domyślnie root) i -h (domyślnie root) są dozwolone
   · Jeśli -h root → pomija wszystkie sprawdzenia (ma wszystko)
   · Jeśli -u niedozwolony → "Nie masz uprawnień jako [użytkownik]."
   · Jeśli -h niedozwolony → "Nie masz uprawnień do uwierzytelniania jako [użytkownik]."
   · Jeśli oba niedozwolone → "Nie masz uprawnień jako [użytkownik] ani do uwierzytelniania jako [użytkownik]."
6. Jeśli OK → permup sprawdza rate limiting (/etc/permup/permup.session):
   · Jeśli użytkownik jest zablokowany → wyświetla: "Zbyt wiele nieudanych prób. Spróbuj ponownie za Xs." i kończy
   · Jeśli nie → przechodzi dalej
7. permup sprawdza, czy -h to root:
   · Jeśli tak → zawsze pyta o hasło roota (nie korzysta z /etc/permup.session)
   · Jeśli nie → sprawdza /etc/permup.session dla pary (-h, -u):
     · Jeśli sesja istnieje i nie wygasła → wysyła zapamiętane hasło automatycznie
     · Jeśli sesja wygasła lub nie istnieje → pyta o hasło interaktywnie
   · Zmiana pary (-h, -u) w stosunku do poprzedniego wywołania powoduje zamknięcie starej sesji i otwarcie nowej (po podaniu prawidłowego hasła)
8. permup wysyła właściwe żądanie do permupd (z hasłem)
9. permupd uwierzytelnia hasło (PAM, /etc/shadow)
   · Jeśli hasło jest nieprawidłowe → klient zwiększa licznik nieudanych prób (rate limiting)
   · Jeśli hasło jest prawidłowe → klient resetuje licznik nieudanych prób
10. Jeśli OK → rodzic wykonuje fork()
11. Rodzic wraca do nasłuchiwania
12. Dziecko:
    · Tworzy PTY (posix_openpt())
    · Uruchamia runuser -l użytkownik_docelowy -c "htop; exit" na slave PTY
    · Czyta z master PTY przez krótki czas (0,5–1s)
    · Sprawdza, czy wyjście zawiera wzorzec z /etc/permup/shell.rc
      · Jeśli -h root → pomija ten test
      · Jeśli -h != root i wykryto wzorzec → zabija proces, zamyka PTY, zwraca błąd
      · Jeśli nie wykryto → przechodzi do następnego kroku
    · Próbuje nawiązać połączenie z terminalem wywoławczym
    · Uruchamia timeout z /etc/permdown.cfg
13. Jeśli połączenie zostanie nawiązane w czasie → sesja trwa
14. Użytkownik korzysta z htopa jako użytkownik docelowy
15. Po zamknięciu htopa:
    · Dziecko kończy się (exit)
    · Klient zamyka połączenie
    · Rodzic czyści zasoby

5.3. Wywołanie przez GUI

1. Użytkownik uruchamia plik .desktop z Exec=permup-gui htop
2. permup-gui łączy się z permupd i wysyła żądanie list_users
3. permupd sprawdza grupy użytkownika wywołującego i zwraca listę dozwolonych -u i -h
4. permup-gui wyświetla okno dialogowe z:
   · Listą rozwijaną dla -u (tylko dozwoleni użytkownicy)
   · Listą rozwijaną dla -h (tylko dozwoleni użytkownicy)
   · Polem na hasło
   · Przyciskiem OK/Anuluj
5. Użytkownik wybiera, wpisuje hasło, zatwierdza
6. permup-gui sprawdza rate limiting (/etc/permup/permup.session):
   · Jeśli użytkownik jest zablokowany → wyświetla komunikat i kończy
   · Jeśli nie → przechodzi dalej
7. permup-gui sprawdza, czy -h to root:
   · Jeśli tak → zawsze pyta o hasło roota (nie korzysta z /etc/permup.session)
   · Jeśli nie → sprawdza /etc/permup.session dla pary (-h, -u):
     · Jeśli sesja istnieje i nie wygasła → wysyła zapamiętane hasło automatycznie
     · Jeśli sesja wygasła lub nie istnieje → pyta o hasło w oknie dialogowym
   · Zmiana pary zamyka starą sesję i otwiera nową
8. permup-gui wysyła właściwe żądanie do permupd (z wybranymi opcjami i hasłem)
9. permupd uwierzytelnia, sprawdza uprawnienia, forkuje dziecko
10. Dziecko tworzy PTY, uruchamia runuser -l użytkownik_docelowy -c "htop; exit"
11. Dziecko czyta z master PTY i sprawdza wzorce z /etc/permup/shell.rc
    · Jeśli -h root → pomija test
    · Jeśli -h != root i wykryto wzorzec → zabija proces, zwraca błąd
12. Próbuje nawiązać połączenie z terminalem wywoławczym
13. Klient nie ma terminala, więc nie może nawiązać połączenia
14. Timeout z /etc/permdown.cfg odlicza (np. 120s)
15. Jeśli htop skończy się przed timeoutem → OK, wynik zwrócony
16. Jeśli timeout minie → dziecko zabija htopa, kończy się, zwraca błąd

5.4. PTY i timeout – jedna faza

· Tylko jedna faza: nawiązanie połączenia z terminalem wywoławczym
· Timeout obowiązuje od momentu uruchomienia komendy do nawiązania połączenia zwrotnego
· Jeśli połączenie zostanie nawiązane → sesja trwa do zamknięcia komendy
· Jeśli nie → po czasie timeout komenda jest zabijana
· Oneshot również działa przez PTY – nie ma osobnego trybu bez PTY

5.5. Blokada powłok (shell block)

· Cel: uniemożliwić uruchamianie powłok (bash, sh, zsh, fish, mc itp.) gdy -h != root i -u == root
· Metoda: dziecko czyta wyjście z PTY i sprawdza wzorce z /etc/permup/shell.rc
· Wyjątek: -h root → pomijamy test (root może uruchamiać powłoki)
· Odporność: metoda działa niezależnie od nazwy pliku, dowiązań symbolicznych, kopii binarek – ponieważ wykrywa zachowanie (prompt), a nie nazwę

5.6. Lista użytkowników (list_users)

· permupd udostępnia żądanie list_users, które dla danego użytkownika wywołującego zwraca:
  · Listę dozwolonych użytkowników docelowych (-u)
  · Listę dozwolonych użytkowników uwierzytelniających (-h)
· Jeśli -h root → zwraca wszystkich użytkowników systemowych
· permup (CLI):
  · Pobiera listę w tle (bez wyświetlania) przed każdym wywołaniem
  · Sprawdza, czy podany -u i -h są dozwolone
  · Jeśli nie → wyświetla odpowiedni komunikat błędu
  · permup -l → wyświetla listę jawnie
· permup-gui (GUI):
  · Pobiera listę i na jej podstawie buduje okno dialogowe z listami rozwijanymi

5.7. Sesja dla pary (-h, -u)

· Sesja jest przypisana do konkretnej pary (użytkownik uwierzytelniający -h, użytkownik docelowy -u)
· Każda para ma własny, niezależny licznik czasu (zgodnie z /etc/permup.session)
· Jeśli użytkownik wywoła permup z tą samą parą i sesja nie wygasła → hasło wysyłane automatycznie
· Jeśli użytkownik wywoła permup z inną parą → stara sesja jest zamykana, otwierana jest nowa (po podaniu prawidłowego hasła)
· Sesje są niezależne dla każdego użytkownika wywołującego – Iksiński i Kowalski mają własne, oddzielne zbiory sesji
· Root (-h root) nie korzysta z sesji – zawsze pyta o hasło

5.8. Rate limiting (blokada po nieudanych próbach)

· Cel: zabezpieczenie przed atakami brute-force
· Metoda: klient liczy nieudane próby uwierzytelnienia dla każdego użytkownika
· Jeśli licznik osiągnie n (z /etc/permup/permup.session), użytkownik jest blokowany na x sekund
· Komunikat: "Zbyt wiele nieudanych prób. Spróbuj ponownie za Xs."
· Licznik resetuje się po pomyślnym uwierzytelnieniu lub po upływie x
· Root (-h root) nie podlega rate limitingowi

6. BEZPIECZEŃSTWO

1. permupd działa jako root, ale nie wykonuje komend – tylko forkuje dzieci
2. Dzieci wykonują komendy przez runuser (bezpieczne przełączanie użytkownika)
3. Dziecko dziedziczy uprawnienia roota TYLKO wtedy, gdy uwierzytelnienie odbyło się hasłem roota (-h root). W przeciwnym razie dziedziczy uprawnienia użytkownika uwierzytelniającego.
4. permup nie ma SUID – nie działa z podniesionymi uprawnieniami
5. Gniazdo Unix ma uprawnienia 0600 – tylko root może się łączyć (lub grupa permup)
6. PTY tworzone przez dzieci, przekazywane przez SCM_RIGHTS – bezpieczne
7. Timeout zabija wiszące procesy – brak zombie
8. Hasła nie są przechowywane – tylko weryfikowane przez PAM
9. GUI nie może przejąć PTY – brak ryzyka przejęcia terminala przez niepowołane procesy
10. Zawsze ; exit – gwarantuje zamknięcie sesji po komendzie
11. Root ma wszystko – nie podlega configowi, co zapobiega przypadkowemu zablokowaniu dostępu
12. Hasło zawsze interaktywnie – brak możliwości podania hasła w linii poleceń (bezpieczniej)
13. Blokada powłok – uniemożliwia uruchamianie powłok z nie-rootowskim uwierzytelnieniem
14. Weryfikacja przez PTY – niezawodna, odporna na dowiązania symboliczne i kopie binarek
15. Ograniczenia czasowe – dodatkowa warstwa zabezpieczeń (ignorowana przez roota)
16. Blokady uż
