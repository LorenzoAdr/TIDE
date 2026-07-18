! Ejemplo mínimo para outline / LSP fortls en tuide.
! Compilar: gfortran -g -o hello hello.f90

program hello
  implicit none
  call greet('tuide')
contains
  subroutine greet(name)
    character(len=*), intent(in) :: name
    print *, 'hello, ', trim(name)
  end subroutine greet
end program hello