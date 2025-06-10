### Routines

A web-server which helps you organize your schedule in an effective way, only works in firefox with an specific ESP32-C6 whereas of now.

## Problems of now.

Chrome javascript doesn't seem like to run the pages properly, or some kind of bug.

Could be optimized and get SSL and HTTPS configuration, but within a proper time-frame is possible.

Not a problem but, you have to put a folder named /Data/ and /Users/creds.json with the login credentials, but i doubt it would matter, the login is yet to be fixed, maybe it works or it gives you a token automatically

this is how creds.json's content is displayed as so.

{
  "users": [
    {
      "username": "user",
      "password": "pass"
    },
    {
      "username": "test",
      "password": "test"
    },
    {
      "username": "admin",
      "password": "admin"
    }
  ]
}
