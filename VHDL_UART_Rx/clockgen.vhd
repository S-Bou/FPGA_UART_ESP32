-- Clock generation baud rate of 115200Hz
library ieee; use ieee.std_logic_1164.all;

entity clockgen is
    generic(baudrate: integer := 50000000);
    port(clk_in, reset: in std_logic;
         clk_out: out std_logic);
end clockgen;

architecture behavioral of clockgen is
    signal counter: integer range 0 to ((baudrate/2)-1);
    signal clk_sig: std_logic;
begin
    process(clk_in)
    begin
        if reset = '0' then
            counter <= 0;
            clk_sig <= '0';
        elsif (clk_in'event and clk_in = '1') then
            if(counter = ((baudrate/2)-1)) then
                clk_sig <= not clk_sig;
                counter <= 0;
            else
                counter <= counter + 1;
            end if;
        end if;
    end process;
    clk_out <= clk_sig;
end behavioral;

-------------------------------------------------------------

library ieee; use ieee.std_logic_1164.all;
package clockgen_package is
    component clockgen
        generic(baudrate: integer := 50000000);
        port(clk_in, reset: in std_logic;
            clk_out: out std_logic);
    end component;
end clockgen_package;