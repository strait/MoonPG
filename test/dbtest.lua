local pg = require'luapg'
local Array = pg.Array

local con = pg.connect('dbname=postgres')

function assert (cond, message)
	if not cond then
		if not message then message = 'assertion failed!' end
		con:run"drop table zipcodes"
		error(message)
	end
end

-- Testing with the zipcodes table
local codes = {
	{'Joppa', 'AL', 35087},
	{'Seward', 'AK', 99664},
	{'Why', 'AZ', 85321},
	{'Guy', 'AR', 72061},
	{'Why', 'AZ', 85321},
	{'Loma Linda', 'CA', 92354},
	{'Eagle', 'CO', 81631},
}

local n, err = con:run"create table zipcodes (city varchar(30), state char(2), code integer);"
for i = 1,#codes do
	local c = codes[i]
	local v,err = con:run("insert into zipcodes values ($1, $2, $3);", c[1], c[2], c[3])
	if not v then print(err) end
end
local res,err = con:run("select count(*) from zipcodes")
-- The number of rows returned as a number.
assert(res[1].count == 7)
-- successful runParams
res = con:run("select city from zipcodes where state = $1", 'AK')
assert(res[1][1] == 'Seward')
-- erronous runParams
res, err = con:run("select code from zipcodes where state = $1", 'AK', 'CA')
assert(res == false)

res, err = con:run("select code from zipcodes where state = $1 and city = $2", 'AK')
assert(res == false)

-- successful prepare
local prep = con:prepare("select code from zipcodes where state = $1 and city = $2")
res = prep:run('AK', 'Seward')
assert(res[1].code == 99664)

-- Prepare with no parameters.
prep = con:prepare("select code from zipcodes where state = 'AK'")
res = prep:run()
assert(res[1].code == 99664)

-- tuples
res = con:run("select * from zipcodes")
--print(res)
--print(con)

local t = {}
for p=1,#res do
	t[#t+1] = res[p].code
end
assert(#t == 7)

local t = {}
for p in res:tuples() do
	t[#t+1] = p[3]
end
assert(#t == 7)

--[[
-- fetch returns all rows
local t1 = res:fetch(true) -- Just first row
local t2 = {res:fetch(true)} -- Capture all
assert(#t1 == 3)
assert(#t2 == 7)
]]
-- columns
local cols = res:fields()
assert(cols[1] == 'city')
assert(cols[3] == 'code')
assert(cols[4] == nil)

con:run"drop table zipcodes"

-- Testing arrays

local  days = {2,5,8,21}
local  nested = {{1,2,5,31},{5,7,8,29}}
local cities = {'Hoho Bam', 'Turlock', 'Paris;'}
con:run"create table schedule (school text, days integer[], cities text[])"
local pr = con:prepare("insert into schedule values ($1, $2, $3)")
local c,err = pr:run("Tupelo High", Array(days), Array(cities))
c,err = pr:run("Nested", Array(nested), Array(cities))

res = con:run"select days, cities from schedule where school = 'Tupelo High'"
local arrs = "days:Array,cities:Array"
res:setTypeMap(arrs)
local b = res[1].days
local c = res[1].cities
assert(#b == 4)
assert(#c == 3)
assert(b[2] == 5)
assert(type(b[2]) == 'number')
assert(c[2] == 'Turlock')
assert(type(c[2]) == 'string')

res = con:run"select days from schedule where school = 'Nested'"
res:setTypeMap(arrs)
b = res[1].days
assert(#b == 2)
assert(#b[1] == 4)
assert(b[1][3] == 5)
-- Clear type map
res:setTypeMap()
assert(res[1].days == '{{1,2,5,31},{5,7,8,29}}')

-- Insert and select arrays with special characters.

local sp1 = 'Wakup"ta,pa'
local sp2 = '{busy} busy\\'
cities = {sp1, nil, "", sp2}
res, err = pr:run('Special', Array(days), Array(cities))
res = con:run"select cities from schedule where school = 'Special'"
res:setTypeMap(arrs)
b = res[1].cities
assert(b[1] == sp1)
assert(b[2] == nil)
assert(b[3] == "")
assert(b[4] == sp2)

con:run"drop table schedule"

-- Test type values
local val1 = 12345.6
local val2 = 1234567890.1234
local val3 = "12345678901234567890.1234"
con:run"create table type_test (a boolean, b real, c double precision, d numeric)"
p,err = con:run("insert into type_test values (TRUE, $1, $2, $3)", val1, val2, val3)
--print(err)
p,err = con:run("insert into type_test values (TRUE, $1, $2, $3)", val1, val1, val1)
res,err = con:run"select * from type_test"
res:setTypeMap('d:String')
assert(res[1][1] == true)
assert(res[1][2] == val1)
assert(res[1][3] == val2)
assert(res[1][4] == val3)
-- Numeric type returned as a string.
assert(res[2][4] == tostring(val1))
-- Numeric returned as number
val1 = 3.14159
con:run("insert into type_test (a, d) values (FALSE, $1)", val1)
res = con:run"select d from type_test where a = FALSE"
assert(res[1].d == val1)

con:run"drop table type_test"

-- Test geometric types
a,err = con:run"create table geo_test (p point, l lseg, b box, t path, r polygon, c circle)"
arrs = "p:Point,l:Line,b:Box,t:Path,r:Polygon,c:Circle"
local val1 = {{34,56},{1,78.4}}
local val2 = {{34,56},{1,78.4},{98,99}}
local val3 = {{42,902},{4,3.1456},{2.181,9}}
con:run("insert into geo_test values ($1, $2, $3, $4, $5, $6)",
	pg.Point(3.6,4.90), pg.Line(54.6, 8, 99.0, 8), pg.Box(val1), pg.Path(val2, true), pg.Polygon(val3),
	pg.Circle(31.6,48.0,5))
con:run("insert into geo_test (t) values ($1)", pg.Path(val2, false))

p,err = con:run"select * from geo_test"
p:setTypeMap(arrs)
local point = p[1].p
assert(point.x == 3.6)
assert(point.y == 4.90)

local pointA, pointB, pointC
local line = p[1].l
pointA = line.a
pointB = line.b
assert(pointA.x == 54.6)
assert(pointA.y == 8)
assert(pointB.x == 99.0)
assert(pointB.y == 8)

local box = p[1].b
pointA = box.ur
pointB = box.ll
assert(pointA.x == 34)
assert(pointA.y == 78.4)
assert(pointB.x == 1)
assert(pointB.y == 56)

local path = p[1].t
pointA = path[1]
pointB = path[2]
pointC = path[3]
assert(pointA.x == 34)
assert(pointA.y == 56)
assert(pointB.x == 1)
assert(pointB.y == 78.4)
assert(pointC.x == 98)
assert(pointC.y == 99)
assert(path.closed == true)

local poly = p[1].r
pointA = poly[1]
pointB = poly[2]
pointC = poly[3]
assert(pointA.x == 42)
assert(pointA.y == 902)
assert(pointB.x == 4)
assert(pointB.y == 3.1456)
assert(pointC.x == 2.181)
assert(pointC.y == 9)

local circle = p[1].c
local c = circle.center
assert(c.x == 31.6)
assert(c.y == 48.0)
assert(circle.radius == 5)

-- Test open path
path = p[2].t
assert(path.closed == false)


con:run"drop table geo_test"

print('All tests Passed!')


--[[
TODO:
  "" and NULL as values.

  ]]
