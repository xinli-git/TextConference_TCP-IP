# TextConference_TCP-IP

### Simple online text conference with linux socket programming

### Welcome!

#### This is a simple text conference program using TPC/IP on linux.
#### All data is not secured during message trasnmission so use it just for fun.

#### To start the program, try `make` to first generate the excutables

### Client:

To initiate the client program, try `client <TCP port number to listen on>` to have the client running on a certain port
Upon success, a message will be printed "Listener successfully running..."
All subsequent log mesage will be printed if there is any client action.


### Server:

To initiate client, simply type `client`

Follow the promted to log-in, join seessoin and chat!\
The complete set of features will be described below
 
*currently, login names and passwords are hardcoded in the server program, please check to see.


## Command and Features:

### List of Features

1. Users talk to each others in sessions, the message will explicity specify from whom and which session the message is coming from
	
2. The login and create session actions will be broadcasted to all the current logged in users, you can also retrive the user and login info later using the list command

3. Each user is allowed to create multiple sessions and join multiple sessions, the currently joined sessions will be explictily printed

4. Sessions expire when all the users leave the session, and when you log out or exit the program, all of your session will be automatically quited as well.


### List of commands

1. `/login <username> <password> <server hostname/ip> <server port number>`

	To login to the server

2. `/createsession <session name>`
		
	To create a session called specified in session name, and join it automatically
	
3. `/joinsession <session name>`
		
	To join the session under the name session name

4. `/leavesession <session name>`
		
	To leave a specific session

5. `/list`
	
	To retrive the current users, sessions and users in each session

6. `/logout`

	Log you out, also leaves all the session you are currently in
		
7. `/quit`
		
	Quit the program, also logs you out

