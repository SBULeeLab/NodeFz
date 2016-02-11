/*
  https://www.npmjs.com/package/sqlite3
  This script follows an SQLite tutorial to see what these DB operations 
    look like at the libuv level.
*/

var sqlite3 = require('sqlite3').verbose();
var db = new sqlite3.Database('mydb.db');

/* Create and populate the table. */
db.serialize(function() {

  db.run("CREATE TABLE if not exists user_info (info TEXT)");
  var stmt = db.prepare("INSERT INTO user_info VALUES (?)");
  for (var i = 0; i < 2; i++) {
      stmt.run("Ipsum " + i);
  }
  stmt.finalize();

  db.each("SELECT rowid AS id, info FROM user_info", function(err, row) {
      console.log(row.id + ": " + row.info);
  });
});
db.close();

/* Wait a second, do more. */
setTimeout(function(){
  console.log("After a second, doing more database stuff\n");
  db = new sqlite3.Database('mydb.db');
  /* Read the table, then add more entries. */
  db.serialize(function() {
    db.each("SELECT rowid AS id, info FROM user_info", function(err, row) {
        console.log(row.id + ": " + row.info);
    });
    var stmt = db.prepare("INSERT INTO user_info VALUES (?)");
    for (var i = 0; i < 2; i++) {
        stmt.run("Ipsum " + i);
    }
    stmt.finalize();
  });
  db.close();
}, 1000);

/* Wait 3 seconds, do more. */
setTimeout(function(){
  console.log("After 3 seconds, doing more database stuff\n");
  db = new sqlite3.Database('mydb.db');
  /* Read the table. */
  db.serialize(function() {
    db.each("SELECT rowid AS id, info FROM user_info", function(err, row) {
        console.log(row.id + ": " + row.info);
    });
  });
  db.close();
}, 3000);

setInterval(function (){ console.log("TIMER"); }, 5000);
