
from string import Template

template_string = open('templates/template_120.txt','r').read()

t=Template(template_string)
unit_dict = {f'UNITS_{i}': f'{i:03}' for i in range(1, 43)}
colors = {f'COLOR_{i}': str(i%4) for i in range(43)}
colors['COLOR_0'] = '4'
print(t.substitute({**unit_dict, **colors}))
