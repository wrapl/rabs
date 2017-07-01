print("Loading builtins")

function stringify(Arg)
	if type(Arg) == "table" then
		local Result = {}
		for I, V in ipairs(Arg) do
			local String = stringify(V)
			if #String > 0 then
				table.insert(Result, String)
			end
		end
		return table.concat(Result, " ")
	else
		return tostring(Arg)
	end
end

function extend(List, ...)
	local Copy = {}
	if List then
		for I, V in ipairs(List) do
			table.insert(Copy, V)
		end
	end
	for I, V in ipairs{...} do
		table.insert(Copy, V)
	end
	return Copy
end

--[[function shell(...)
	local Command = stringify(...)
	local Input = io.popen(Command)
	local Result = Input:read("a")
	Input:close()
	return Result
end]]--
