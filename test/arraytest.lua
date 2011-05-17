local pg = require'luapg'

local con = pg.connect('dbname=postgres')

function assert (cond, message)
  if not cond then
    if not message then message = 'assertion failed!' end
    con:run"drop table zipcodes"
    error(message)
  end
end

local arr = {'John',8,7,'Jack',14,16}
local loc = 'Vancouver'
local n = con:run"create table tides (location varchar(30), highest text[]);"
con:runParams("insert into tides values ($1, $2)", loc, arr)
res = con:run("select highest from tides")
p = res:fetch()
o = p['highest']

for i=1,#o do
  print(o[i])
end
con:run"drop table tides"

