function Animal() {
  this.makeNoise = function() { console.log("Animal makes noise"); }; 
  this.nLegs = undefined;
  this.fur = undefined;
}

function Dog() {
  var d = new Animal();
  d.makeNoise = function() { console.log("Dog says bark!"); };
  d.nLegs = 4;
  d.fur = "yes";
  return d;
}

function PitBull() {
  var d = new Dog();
  d.makeNoise = function() { console.log("Pitbull says grrrr!"); };
  d.nLegs = 4;
  d.fur = "yes";
  return d;
}

function Turtle() {
  var t = new Animal();
  t.makeNoise = function() { console.log("Turtle says: ???"); };
  t.nLegs = 4;
  t.fur = "no";
  return t;
}

var animal = new Animal();
var dog = new Dog();
var turtle = new Turtle();
var pitBull = new PitBull();

animal.makeNoise();
dog.makeNoise();
turtle.makeNoise();
pitBull.makeNoise();

dog.nLegs = 3; // hit by a car
console.log("Dog has " + dog.nLegs + " legs");
var dog2 = Object.create(dog);
dog2.nlegs = 4;
console.log("Dog has " + dog.nLegs + " legs; dog2 has " + dog2.nLegs);
