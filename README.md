# backup-share-cloud-2016
Tema 2 PC
	Sarariu Liviu - Dan 323CC
	
Usage & commands in short:

	Usage
		./server <port_server> <users_config_file> <static_shares_config_file>
		./client <IP_server> <port_server>
		Note: I've included samples for users_config_file and static_shares_config_file.		
	Commands
	 client
		login
		logout
		upload
		download
		share
		unshare
		getuserlist
		getfilelist
		delete
		quit
	 server
	 	quit
	 	
Pentru mai multe detalii consultati enuntul.

Detaliere protocol folosit:

	- fiecarei comenzi i s-a atribuit un numar (numarul aferent fiecarei comenzi in enunt: 1.login, 2.logout, 3.getfilelist, etc)
	- pe retea se trimite numarul comenzi si nu textul efectiv(1 in loc de login, etc)
	- dupa numarul comenzii e pus marimea frameului trimis pe retea si apoi frameul efectiv cu datele(cu exceptia cazurilor in 
	  care marimea se cunoaste si nu este nevoie sa fie trimisa)
	- pentru transmiterea bucatilor de fisiere am folosit ca si numar de comanda 0
	- in trasmiterea bucatilor de fisiere nu se specifica marimea frameului, aceasta fiind constanta(4096 - sizeof(int)), cu exceptia
	  ultimei bucati, care poate fi mai mica
	- inainte de ultima bucata de fisier se trimite un frame cu marimea acestuia
	- pentru comanda quit de la client nu am mai trimis mesaj serverului ca acel client vrea sa inchida. Nu am considerat ca e relevant
	  acel mesaj. Clientul pur si simplu nu mai accepta comenzi de la user, termina de transferat fisierele si inchide.
	- pentru quit la server in schimb, am trimis mesaj fiecarui client ca userii sa stie ca serverul va ignora comenzile lor(clientul 
	  nici nu mai trimite mesajele primite de la user, dar in acelasi timp nici serverul nu mai accepta, chiar daca ar primi; redundanta
	  e din motiv de securitate; chiar daca pun la client sa nu mai trimita mesajele primite dupa quit, din motive de eficienta(pentru a nu mai 
	  trimite la server date inutile) nu se poate insa garanta la server ca nu va mai primi comenzi, sau nu e bine sa fie luat ca garantat acest 
      lucru)
	- tot din motiv de securitate desi clientul verifica la fiecare comanda daca userul e logat si daca nu e nu mai trimite comanda, si la server
	  se verifica daca userul e logat, la primirea unei comenzi.
	- daca se da quit in timp ce sunt transferuri in desfasurare, dupa terminare si in inchidere, in fisierul de log nu mai apar mesajele "Upload finished.. "/"Download finished.." de la final. 
	Uploadul/Downloadul se termina,datele sunt trimise toate, nu se mai asteapta insa si mesajul de call-back, pentru a se inchide cat mai repede. Se garanteaza transmisia corecta si 
    in totalitate datorita substratului TCP, deci acel mesaj de call back are doar rolul de a instiinta utilizatorul de terminarea uploadului/downloadului, pentru ca acesta sa stie ca
	 poate initia un upload/download nou de exemplu.
	- din cauza implementarilor bufferelor socketilor TCP, in cazul de mai sus aceste ultime mesaje de call-back pot totusi aparea in fisierului de log(cel mai frecvent in cazul de download), dar 
	  nu se garanteaza acest lucru.

Detalii de implementare:

	- pe langa bufferul dat ca parametru, am facut un buffer persistent la recv(atat la server(cate unul pentru fiecare client) cat si la clienti) in care sa se tina datele in caz
	ca se primesc mai multe frameuri sau nu s.a primit un frame complet(lucru posibil pe conexiuni mai slabe, caz in care chiar daca se da un send cu un frame, in partea opusa
	nu e obligatoriu sa ajunga frameul intr-un singur recv(fapt pentru care ma uit la lungimea returnata de recv sa stiu daca am un frame complet) )
	- am retinut detaliile aferente fiecarui client/user/fisier in mai multe mapuri ce contin structuri definite de mine ce grupeaza aceste date(client_info, user_info, file_info)
